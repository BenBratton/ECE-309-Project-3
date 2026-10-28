// src/tools/tool_registry.cpp — hash table from tool name to Tool (spec §4.3).
//
// Design: separate chaining, FNV-1a 64-bit hash, power-of-two bucket count,
// double the buckets whenever the next add would push size/buckets above 1.0.

#include "tools/tool_registry.h"

#include <cstdint>
#include <utility>

ToolRegistry::ToolRegistry() : buckets_(kInitialBuckets) {}

// Each Entry's unique_ptr deletes its Tool when the vectors are destroyed.
ToolRegistry::~ToolRegistry() = default;

// FNV-1a: XOR in each byte, then multiply by the FNV prime. Uses every
// character and is order-sensitive. Returns the full-width value.
std::size_t ToolRegistry::hash(std::string_view key) noexcept {
    std::uint64_t h = 1469598103934665603ULL;          // offset basis
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;                         // FNV prime
    }
    return static_cast<std::size_t>(h);
}

// Move every entry into a fresh bucket array, recomputing each index with
// the NEW bucket count. Only unique_ptrs move; the Tool objects stay put.
void ToolRegistry::rehash(std::size_t new_bucket_count) {
    std::vector<std::vector<Entry>> fresh(new_bucket_count);
    for (auto& chain : buckets_) {
        for (auto& e : chain) {
            std::size_t idx = e.hash & (new_bucket_count - 1);
            fresh[idx].push_back(std::move(e));
        }
    }
    buckets_.swap(fresh);   // old (now empty-pointer) entries die with `fresh`
}

bool ToolRegistry::add(std::unique_ptr<Tool> tool) {
    if (tool == nullptr) return false;

    std::string name = tool->name();
    std::size_t h = hash(name);

    // Duplicate? Keep the original; `tool` is destroyed when we return.
    for (const Entry& e : buckets_[index_for(h)]) {
        if (e.hash == h && e.name == name) return false;
    }

    // Check the load factor BEFORE inserting (spec pitfall 3).
    if (static_cast<double>(size_ + 1) / static_cast<double>(buckets_.size()) > kMaxLoadFactor) {
        rehash(buckets_.size() * 2);
    }

    buckets_[index_for(h)].push_back(Entry{std::move(name), h, std::move(tool)});
    ++size_;
    return true;
}

Tool* ToolRegistry::find(std::string_view name) const {
    std::size_t h = hash(name);
    for (const Entry& e : buckets_[index_for(h)]) {
        if (e.hash == h && e.name == name) return e.tool.get();   // no ownership transfer
    }
    return nullptr;
}

std::size_t ToolRegistry::size() const noexcept { return size_; }

void ToolRegistry::for_each(const std::function<void(const Tool&)>& fn) const {
    for (const auto& chain : buckets_)
        for (const Entry& e : chain) fn(*e.tool);
}

std::size_t ToolRegistry::bucket_count() const noexcept { return buckets_.size(); }

double ToolRegistry::load_factor() const noexcept {
    return static_cast<double>(size_) / static_cast<double>(buckets_.size());
}

std::size_t ToolRegistry::max_probe_length() const {
    std::size_t longest = 0;
    for (const auto& chain : buckets_)
        if (chain.size() > longest) longest = chain.size();
    return longest;
}

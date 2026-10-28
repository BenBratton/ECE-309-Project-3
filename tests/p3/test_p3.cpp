// tests/p3/test_p3.cpp — YOUR P3 TEST SUITE.
//
// The five tests below are worked examples. Keep them, and add your own
// until you have AT LEAST 15 (spec §6 lists what must be covered).
// Name every test <area>_<what>: reg_ for the registry, ws_ for the
// workspace, calc_ for the calculator.
//
// Build & run:   cmake --build build && ./build/test_p3
// One area:      ./build/test_p3 reg_        (names starting with "reg_")
// One test:      ./build/test_p3 =reg_add_and_find

#include "../mini_test.h"
#include "../test_helpers.h"     // TempDir, write_text, args1, ...

#include "tools/calculator.h"
#include "tools/tool_registry.h"
#include "tools/workspace.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A tool whose name you choose — use it to fill the registry in tests.
class NamedTool : public Tool {
public:
    explicit NamedTool(std::string n) : n_(std::move(n)), schema_(JsonValue::object()) {}
    std::string name() const override { return n_; }
    std::string description() const override { return "test tool"; }
    const JsonValue& schema() const override { return schema_; }
    ToolResult invoke(const JsonValue&, ToolContext&) override { return ToolResult::success(n_); }
private:
    std::string n_;
    JsonValue   schema_;   // an empty JSON object: {}
};

// Tokenize with the provided tokenizer, then run YOUR calc_evaluate.
bool eval(const std::string& expr, double* value, std::string* error) {
    std::vector<CalcToken> tokens;
    if (!calc_tokenize(expr, &tokens, error)) return false;
    return calc_evaluate(tokens, value, error);
}

}  // namespace

// ---------------------------------------------------------------- examples

TEST(reg_add_and_find) {
    ToolRegistry r;
    auto t = std::make_unique<NamedTool>("alpha");
    Tool* raw = t.get();                        // remember the address before moving
    REQUIRE(r.add(std::move(t)));
    CHECK(r.size() == 1);
    CHECK(r.find("alpha") == raw);              // same object, not a copy
    CHECK(r.find("beta") == nullptr);
}

TEST(reg_duplicate_keeps_original) {
    ToolRegistry r;
    auto first = std::make_unique<NamedTool>("alpha");
    Tool* original = first.get();
    REQUIRE(r.add(std::move(first)));
    CHECK(!r.add(std::make_unique<NamedTool>("alpha")));
    CHECK(r.find("alpha") == original);
    CHECK(r.size() == 1);
}

TEST(ws_rejects_parent_escape) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("notes/todo.txt", &out, &reason));   // sanity: accepts *something*
    CHECK(out == ws.root() / "notes" / "todo.txt");
    CHECK(!ws.resolve("../etc/passwd", &out, &reason));
    CHECK(!reason.empty());
}

TEST(calc_precedence) {
    double v = 0;
    std::string err;
    REQUIRE(eval("2 + 3 * 4", &v, &err));
    CHECK(v == 14);
}

TEST(calc_division_by_zero) {
    double v = 0;
    std::string err;
    CHECK(!eval("1 / 0", &v, &err));
    CHECK(err == "division by zero");
}

// ------------------------------------------------------------- registry

TEST(reg_empty_table) {
    ToolRegistry r;
    CHECK(r.size() == 0);
    CHECK(r.bucket_count() >= 1 && r.bucket_count() <= 64);
    CHECK(r.load_factor() == 0.0);
    CHECK(r.max_probe_length() == 0);
    CHECK(r.find("anything") == nullptr);
}

TEST(reg_null_rejected) {
    ToolRegistry r;
    CHECK(!r.add(nullptr));
    CHECK(r.size() == 0);
    REQUIRE(r.add(std::make_unique<NamedTool>("a")));
    CHECK(!r.add(std::unique_ptr<Tool>{}));
    CHECK(r.size() == 1);
}

TEST(reg_find_absent_and_near_miss) {
    ToolRegistry r;
    REQUIRE(r.add(std::make_unique<NamedTool>("read_file")));
    CHECK(r.find("read_fil") == nullptr);
    CHECK(r.find("read_file ") == nullptr);
    CHECK(r.find("READ_FILE") == nullptr);
    CHECK(r.find("") == nullptr);
    CHECK(r.find("read_file") != nullptr);
}

TEST(reg_for_each_visits_each_once) {
    ToolRegistry r;
    std::vector<std::string> names = {"read_file", "write_file", "list_dir", "calculator", "echo"};
    for (const auto& n : names) REQUIRE(r.add(std::make_unique<NamedTool>(n)));
    std::vector<int> seen(names.size(), 0);
    int count = 0;
    r.for_each([&](const Tool& t) {
        ++count;
        for (std::size_t i = 0; i < names.size(); ++i)
            if (t.name() == names[i]) ++seen[i];
    });
    CHECK(count == static_cast<int>(names.size()));
    for (int s : seen) CHECK(s == 1);
}

TEST(reg_hash_is_order_sensitive) {
    CHECK(ToolRegistry::hash("ab") != ToolRegistry::hash("ba"));
    CHECK(ToolRegistry::hash("tool_1234") != ToolRegistry::hash("tool_1243"));
    CHECK(ToolRegistry::hash("x") == ToolRegistry::hash("x"));   // deterministic
}

TEST(reg_10000_tools) {
    ToolRegistry r;
    auto first = std::make_unique<NamedTool>("tool_0");
    Tool* first_ptr = first.get();
    REQUIRE(r.add(std::move(first)));
    CHECK(r.load_factor() <= 1.0);
    bool lf_ok = true;
    for (int i = 1; i < 10000; ++i) {
        REQUIRE(r.add(std::make_unique<NamedTool>("tool_" + std::to_string(i))));
        if (r.load_factor() > 1.0) lf_ok = false;
    }
    CHECK(lf_ok);
    CHECK(r.size() == 10000);
    CHECK(r.find("tool_0") == first_ptr);          // survived every resize
    bool all_found = true;
    for (int i = 0; i < 10000; ++i) {
        std::string n = "tool_" + std::to_string(i);
        Tool* t = r.find(n);
        if (t == nullptr || t->name() != n) all_found = false;
    }
    CHECK(all_found);
    CHECK(r.find("tool_10000") == nullptr);
    CHECK(r.max_probe_length() <= 64);

    std::size_t distinct_check = 0;   // sanity: hash spreads the names
    {
        std::vector<std::size_t> hs;
        for (int i = 0; i < 10000; ++i) hs.push_back(ToolRegistry::hash("tool_" + std::to_string(i)));
        std::sort(hs.begin(), hs.end());
        distinct_check = static_cast<std::size_t>(std::unique(hs.begin(), hs.end()) - hs.begin());
    }
    CHECK(distinct_check >= 9990);

    std::printf("  [FNV-1a chaining] bucket_count=%zu load_factor=%.4f max_probe_length=%zu\n",
                r.bucket_count(), r.load_factor(), r.max_probe_length());
}

TEST(reg_alternative_hash) {
    ToolRegistry r;
    for (int i = 0; i < 10000; ++i)
        REQUIRE(r.add(std::make_unique<NamedTool>("tool_" + std::to_string(i))));
    const std::size_t buckets = r.bucket_count();

    std::vector<int> counts(buckets, 0);
    for (int i = 0; i < 10000; ++i) {
        std::string n = "tool_" + std::to_string(i);
        std::size_t weak = static_cast<unsigned char>(n[0]) + n.size();   // s[0] + s.size()
        ++counts[weak % buckets];
    }
    int weak_longest = *std::max_element(counts.begin(), counts.end());

    std::printf("  [weak s[0]+size]  buckets=%zu longest_chain=%d   vs FNV-1a %zu\n",
                buckets, weak_longest, r.max_probe_length());
    CHECK(static_cast<std::size_t>(weak_longest) > r.max_probe_length());
}

TEST(reg_tools_destroyed_once) {
    static int alive = 0;
    struct Counted : NamedTool {
        explicit Counted(std::string n) : NamedTool(std::move(n)) { ++alive; }
        ~Counted() override { --alive; }
    };
    alive = 0;
    {
        ToolRegistry r;
        for (int i = 0; i < 100; ++i) r.add(std::make_unique<Counted>("t" + std::to_string(i)));
        CHECK(alive == 100);
        CHECK(!r.add(std::make_unique<Counted>("t5")));   // duplicate destroyed right away
        CHECK(alive == 100);
    }
    CHECK(alive == 0);
}

// ------------------------------------------------------------- workspace

namespace {
struct WsCase { std::string in; bool ok; fs::path rel; };   // rel "" means root
}

TEST(ws_spec_table) {
    TempDir d;
    Workspace ws(d.path());
    std::vector<WsCase> cases = {
        {"notes/todo.txt",    true,  "notes/todo.txt"},
        {"./notes//todo.txt", true,  "notes/todo.txt"},
        {".",                 true,  ""},
        {"a/../b",            true,  "b"},
        {"a/..",              true,  ""},
        {"new/dir/file.txt",  true,  "new/dir/file.txt"},
        {"my..notes.txt",     true,  "my..notes.txt"},
        {"",                  false, ""},
        {"/etc/passwd",       false, ""},
        {"..",                false, ""},
        {"../etc/passwd",     false, ""},
        {"a/../../b",         false, ""},
    };
    for (const auto& c : cases) {
        fs::path out;
        std::string reason;
        bool ok = ws.resolve(c.in, &out, &reason);
        CHECK(ok == c.ok);
        if (ok && c.ok) {
            fs::path want = c.rel.empty() ? ws.root() : ws.root() / c.rel;
            CHECK(out == want);
        }
        if (!ok) CHECK(!reason.empty());
    }
}

TEST(ws_rejects_nul_byte) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    std::string p = std::string("notes") + '\0' + "/x";
    CHECK(!ws.resolve(p, &out, &reason));
    CHECK(!reason.empty());
}

TEST(ws_rejects_escape_and_return) {
    TempDir d;
    fs::path root = d.path() / "ws";
    fs::create_directories(root);
    Workspace ws(root);
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve("notes/../../ws/notes/todo.txt", &out, &reason));
    CHECK(!reason.empty());
}

TEST(ws_does_not_touch_filesystem) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("brand/new/deep/file.txt", &out, &reason));
    CHECK(out == ws.root() / "brand" / "new" / "deep" / "file.txt");
    CHECK(!fs::exists(ws.root() / "brand"));   // nothing was created
}

// ------------------------------------------------------------- calculator

TEST(calc_spec_table) {
    struct C { const char* e; double v; };
    C cases[] = {
        {"2 + 3 * 4", 14}, {"10 - 4 - 3", 3}, {"100 / 10 / 5", 2},
        {"(2 + 3) * 4", 20}, {"2 * (3 + 4) - 5", 9}, {"-3 * -3", 9}, {"7 / 2", 3.5},
    };
    for (const auto& c : cases) {
        double v = 0;
        std::string err;
        CHECK(eval(c.e, &v, &err));
        CHECK(v == c.v);
    }
    double v = 0;
    std::string err;
    CHECK(!eval("1 / (2 - 2)", &v, &err));
    CHECK(err == "division by zero");
}

TEST(calc_left_to_right) {
    double v = 0;
    std::string err;
    REQUIRE(eval("8 / 4 * 2", &v, &err));    CHECK(v == 4);    // not 1
    REQUIRE(eval("1 - 2 + 3", &v, &err));    CHECK(v == 2);    // not -4
}

TEST(calc_nested_and_decimals) {
    double v = 0;
    std::string err;
    REQUIRE(eval("((1.5 + 2.5) * (3 - 1)) / 4", &v, &err));  CHECK(v == 2);
    REQUIRE(eval("(-1)", &v, &err));                         CHECK(v == -1);
    REQUIRE(eval("42", &v, &err));                           CHECK(v == 42);
}

TEST(calc_long_expressions) {
    std::string sum = "1";
    for (int i = 1; i < 20000; ++i) sum += "+1";
    double v = 0;
    std::string err;
    REQUIRE(eval(sum, &v, &err));
    CHECK(v == 20000);

    std::string nested = std::string(5000, '(') + "7" + std::string(5000, ')');
    REQUIRE(eval(nested, &v, &err));
    CHECK(v == 7);
}

TEST(calc_malformed_tokens_do_not_crash) {
    using K = CalcToken::Kind;
    double v = 0;
    std::string err;
    CHECK(!calc_evaluate({}, &v, &err));                                        CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({{K::Plus, 0}}, &v, &err));                            CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({{K::RParen, 0}}, &v, &err));                          CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({{K::LParen, 0}, {K::Number, 1}}, &v, &err));          CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({{K::Number, 1}, {K::Number, 2}}, &v, &err));          CHECK(!err.empty());
}

int main(int argc, char** argv) { return mt::run_all(argc, argv); }

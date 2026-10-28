// src/tools/workspace.cpp — Workspace::resolve (spec §4.4).
//
// Purely lexical: never touches the filesystem, never throws.

#include "tools/workspace.h"

#include <exception>

namespace fs = std::filesystem;

bool Workspace::resolve(std::string_view user_path, fs::path* out,
                        std::string* reason) const {
    // 1. Empty path.
    if (user_path.empty()) {
        *reason = "empty path";
        return false;
    }
    // 2. Embedded NUL: the OS would silently truncate the path there.
    if (user_path.find('\0') != std::string_view::npos) {
        *reason = "path contains a NUL byte";
        return false;
    }
    try {
        // 3. Only workspace-relative paths.
        fs::path p{std::string(user_path)};
        if (p.is_absolute() || p.has_root_path()) {
            *reason = "absolute paths are not allowed: " + std::string(user_path);
            return false;
        }
        // 4. Fold ".", "//" and "x/.." textually.
        fs::path n = p.lexically_normal();

        // 5. A leading ".." after normalizing means the path climbed above root.
        if (n.begin() != n.end() && *n.begin() == "..") {
            *reason = "path escapes the workspace: " + std::string(user_path);
            return false;
        }
        // 6. Build the absolute result.
        if (n == ".") *out = root();
        else          *out = root() / n;
        return true;
    } catch (const std::exception&) {   // e.g. bad_alloc; never let it escape
        *reason = "could not process path";
        return false;
    }
}

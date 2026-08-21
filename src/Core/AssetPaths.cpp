#include <Core/AssetPaths.h>

#include <raylib.h>

namespace {

/// A file that must exist under a real asset root.
///
/// Probing for the directory alone is not enough: an empty `assets/` left over
/// beside a binary would win the search and then every load would fail one by
/// one, which is the failure this whole file exists to prevent. A shader the
/// renderer cannot start without is a good witness.
const char *const kSentinel = "shaders/glsl330/level.fs";

/// Candidate roots, in the order they are tried, relative to the executable.
///
/// The first is the shipped layout and the one that matters. The rest are the
/// ways a developer's binary sits at a different depth than a player's:
/// CMake puts it in build/bin, so the repo's assets are two levels up, and a
/// macOS .app keeps them beside the executable's parent in Resources.
const char *const kCandidates[] = {
    "assets",                 // <exe>/assets            -- shipped
    "../assets",              // <exe>/../assets
    "../../assets",           // build/bin/Game -> repo/assets
    "../Resources/assets",    // Game.app/Contents/MacOS -> .../Resources
};

std::string join(const std::string &a, const std::string &b) {
    if (a.empty()) return b;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "/" + b;
}

std::string &cachedRoot() {
    static std::string root;
    return root;
}

bool &resolved() {
    static bool done = false;
    return done;
}

} // namespace

namespace assets {

bool resolve() {
    if (resolved()) return !cachedRoot().empty();
    resolved() = true;

    // Static buffer inside raylib, so copy it before anything else calls a
    // path function and overwrites it.
    const std::string exe_dir = GetApplicationDirectory();

    for (const char *candidate : kCandidates) {
        const std::string root = join(exe_dir, candidate);
        if (!FileExists(join(root, kSentinel).c_str())) continue;
        cachedRoot() = root;
        TraceLog(LOG_INFO, "assets: root is '%s'", root.c_str());
        return true;
    }

    // Last resort: the directory this binary was built against. Keeps a
    // developer's build working when it is run from somewhere unusual, and is
    // deliberately last so a packaged copy can never silently reach back into
    // a source tree that happens to be on the same machine.
#ifdef SOURCE_ASSET_DIR
    if (FileExists(join(SOURCE_ASSET_DIR, kSentinel).c_str())) {
        cachedRoot() = SOURCE_ASSET_DIR;
        TraceLog(LOG_WARNING,
                 "assets: no assets directory next to the executable; falling "
                 "back to the build-time source tree '%s'. A packaged copy of "
                 "this build would find nothing.",
                 SOURCE_ASSET_DIR);
        return true;
    }
#endif

    TraceLog(LOG_FATAL, "assets: no asset root found. Looked for '%s' under:",
             kSentinel);
    for (const char *candidate : kCandidates) {
        TraceLog(LOG_FATAL, "assets:   %s", join(exe_dir, candidate).c_str());
    }
    TraceLog(LOG_FATAL,
             "assets: a package is the executable with an 'assets' directory "
             "beside it. Copy or symlink one there.");
    return false;
}

const std::string &root() {
    if (!resolved()) resolve();
    return cachedRoot();
}

std::string path(const std::string &relative) {
    return join(root(), relative);
}

} // namespace assets

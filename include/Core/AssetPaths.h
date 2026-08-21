#pragma once

#include <string>

/// Where the game's assets are, found relative to the running executable.
///
/// This exists because the alternative did not survive being shipped. ASSET_DIR
/// used to be a compile-time string holding CMAKE_SOURCE_DIR -- an absolute
/// path into the machine that built the binary. The game ran perfectly there
/// and, on anyone else's computer, came up with no models, no shaders and the
/// fallback ground plane, because every asset path pointed at a directory that
/// did not exist. Nothing about that failure says "wrong path": it looks like a
/// broken build.
///
/// So the root is resolved at startup by looking next to the executable, and a
/// package is a binary with an `assets` directory beside it, wherever it lands.
namespace assets {

/// The asset root, without a trailing separator. Resolved on first use and
/// cached. Empty only if resolve() failed, which is fatal and already reported.
const std::string &root();

/// `relative` under the asset root, e.g. path("shaders/glsl330/level.fs").
///
/// Takes a path with no leading separator: callers read as a path relative to
/// the assets directory, which is what it is.
std::string path(const std::string &relative);

/// Find the asset root, log which candidate won, and return false if none did.
///
/// Call once at startup, before anything loads. Calling it is optional -- root()
/// resolves lazily -- but doing it explicitly is what lets the program refuse to
/// start with a message, instead of opening a window and failing asset by asset.
bool resolve();

} // namespace assets

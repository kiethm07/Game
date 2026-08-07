#pragma once

#include <raylib.h>
#include <string>

/// Shader loading with `#include` support.
///
/// GLSL 330 has no preprocessor include, so shared code between shaders had to
/// be copy-pasted. The shadow sampling block was ~145 lines duplicated verbatim
/// into world.fs and skinning.fs, each carrying a comment asking whoever edited
/// one to remember the other. Adding a third receiver (level.fs) would have made
/// it three copies to keep in step by hand.
///
/// This splices the text in before handing the result to the driver, which is
/// all a preprocessor include ever was.
namespace ShaderLibrary {

/// Load a vertex/fragment pair, resolving `#include "file.glsl"` directives in
/// both. Include paths are relative to the including file's own directory.
///
/// Behaves like LoadShader on failure: returns a Shader whose id is 0 after
/// logging what went wrong. A missing include is an error rather than a
/// silently skipped line — a shader missing half its definitions produces a
/// wall of driver errors that say nothing about the actual cause.
Shader load(const std::string &vsPath, const std::string &fsPath);

/// The include-resolved text of one file, exposed for tests and for callers
/// that want to compile a shader from memory themselves. Returns an empty
/// string if the file or one of its includes cannot be read.
std::string preprocess(const std::string &path);

} // namespace ShaderLibrary

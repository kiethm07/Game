#include <Rendering/ShaderLibrary.h>

#include <fstream>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace {

/// Guard against an include cycle, and against a diamond pulling the same
/// declarations in twice. Every file is spliced at most once per translation,
/// which makes includes behave like `#pragma once` without needing one.
using SeenSet = std::unordered_set<std::string>;

std::string directoryOf(const std::string &path) {
    const size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string(".") : path.substr(0, cut);
}

/// The quoted filename in `#include "name"`, or empty if the line is not one.
/// Deliberately narrow: only a leading `#include` with a double-quoted argument
/// counts, because that is the whole syntax this needs to support.
std::string includeTarget(const std::string &line) {
    size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos || line[i] != '#') return {};

    static const std::string kDirective = "#include";
    if (line.compare(i, kDirective.size(), kDirective) != 0) return {};
    i += kDirective.size();

    const size_t open = line.find('"', i);
    if (open == std::string::npos) return {};
    // Anything between the directive and the quote must be whitespace,
    // otherwise this is some other token that merely starts with #include.
    if (line.find_first_not_of(" \t", i) != open) return {};

    const size_t close = line.find('"', open + 1);
    if (close == std::string::npos) return {};
    return line.substr(open + 1, close - open - 1);
}

bool expand(const std::string &path, SeenSet &seen, std::ostringstream &out) {
    std::ifstream file(path);
    if (!file) {
        TraceLog(LOG_ERROR, "ShaderLibrary: cannot open '%s'", path.c_str());
        return false;
    }

    const std::string dir = directoryOf(path);
    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        lineNumber++;
        const std::string target = includeTarget(line);
        if (target.empty()) {
            out << line << '\n';
            continue;
        }

        const std::string resolved = dir + "/" + target;
        if (!seen.insert(resolved).second) {
            // Already spliced. Leave a comment rather than nothing so the line
            // numbers a driver reports still line up with something readable.
            out << "// (already included: " << target << ")\n";
            continue;
        }

        if (!expand(resolved, seen, out)) {
            TraceLog(LOG_ERROR, "ShaderLibrary:   included from %s:%d",
                     path.c_str(), lineNumber);
            return false;
        }
    }

    return true;
}

} // namespace

std::string ShaderLibrary::preprocess(const std::string &path) {
    SeenSet seen;
    std::ostringstream out;
    if (!expand(path, seen, out)) return {};
    return out.str();
}

Shader ShaderLibrary::load(const std::string &vsPath,
                           const std::string &fsPath) {
    const std::string vs = preprocess(vsPath);
    const std::string fs = preprocess(fsPath);
    if (vs.empty() || fs.empty()) {
        TraceLog(LOG_ERROR, "ShaderLibrary: failed to assemble %s + %s",
                 vsPath.c_str(), fsPath.c_str());
        return Shader{};
    }

    // LoadShaderFromMemory rather than LoadShader: the text handed to the
    // driver is the spliced result, which exists nowhere on disk.
    return LoadShaderFromMemory(vs.c_str(), fs.c_str());
}

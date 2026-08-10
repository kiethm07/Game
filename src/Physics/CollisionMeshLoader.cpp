#include <Physics/CollisionMeshLoader.h>

#include <raylib.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

constexpr char kMagic[4] = {'S', 'K', 'C', 'M'};
constexpr std::uint32_t kVersion = 1;

/// Refuses a header that would make the reader allocate wildly on a truncated
/// or unrelated file. The castle's mesh is ~46k vertices; ten million is far
/// past anything the pipeline can produce and still cheap to reject.
constexpr std::uint32_t kSanityLimit = 10u * 1000u * 1000u;

bool readExact(std::ifstream &file, void *dst, std::size_t bytes) {
    file.read(static_cast<char *>(dst), static_cast<std::streamsize>(bytes));
    return static_cast<std::size_t>(file.gcount()) == bytes;
}

} // namespace

bool CollisionMeshLoader::load(const std::string &path, CollisionMesh &out) {
    out = CollisionMesh{};

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        TraceLog(LOG_ERROR, "CollisionMeshLoader: cannot open '%s'",
                 path.c_str());
        return false;
    }

    char magic[4] = {};
    std::uint32_t version = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;

    if (!readExact(file, magic, sizeof(magic)) ||
        std::memcmp(magic, kMagic, sizeof(magic)) != 0) {
        TraceLog(LOG_ERROR,
                 "CollisionMeshLoader: '%s' is not a collision mesh (bad magic)",
                 path.c_str());
        return false;
    }
    if (!readExact(file, &version, sizeof(version)) || version != kVersion) {
        TraceLog(LOG_ERROR,
                 "CollisionMeshLoader: '%s' is version %u, this build reads %u. "
                 "Re-export it with tools/export_level.py.",
                 path.c_str(), version, kVersion);
        return false;
    }
    if (!readExact(file, &vertex_count, sizeof(vertex_count)) ||
        !readExact(file, &triangle_count, sizeof(triangle_count))) {
        TraceLog(LOG_ERROR, "CollisionMeshLoader: '%s' has a truncated header",
                 path.c_str());
        return false;
    }
    if (vertex_count == 0 || triangle_count == 0 ||
        vertex_count > kSanityLimit || triangle_count > kSanityLimit) {
        TraceLog(LOG_ERROR,
                 "CollisionMeshLoader: '%s' declares %u vertices and %u "
                 "triangles, which is not a level's collision mesh",
                 path.c_str(), vertex_count, triangle_count);
        return false;
    }

    std::vector<float> positions(static_cast<std::size_t>(vertex_count) * 3);
    std::vector<std::uint32_t> indices(
        static_cast<std::size_t>(triangle_count) * 3);

    if (!readExact(file, positions.data(), positions.size() * sizeof(float)) ||
        !readExact(file, indices.data(),
                   indices.size() * sizeof(std::uint32_t))) {
        TraceLog(LOG_ERROR,
                 "CollisionMeshLoader: '%s' ends early — expected %u vertices "
                 "and %u triangles",
                 path.c_str(), vertex_count, triangle_count);
        return false;
    }

    std::vector<Vector3> vertices(vertex_count);
    for (std::uint32_t i = 0; i < vertex_count; ++i) {
        vertices[i] = {positions[i * 3 + 0], positions[i * 3 + 1],
                       positions[i * 3 + 2]};
    }

    // Range-checked here rather than trusted: CollisionMesh::build drops
    // out-of-range triangles, but silently, and a file whose indices are all
    // out of range would produce an empty mesh and a level with no ground.
    std::vector<int> soup(indices.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] >= vertex_count) {
            TraceLog(LOG_ERROR,
                     "CollisionMeshLoader: '%s' index %zu is %u, past the %u "
                     "vertices it declares",
                     path.c_str(), i, indices[i], vertex_count);
            return false;
        }
        soup[i] = static_cast<int>(indices[i]);
    }

    const double started = GetTime();
    out.build(std::move(vertices), std::move(soup));
    const double build_ms = (GetTime() - started) * 1000.0;

    if (out.isEmpty()) {
        TraceLog(LOG_ERROR,
                 "CollisionMeshLoader: '%s' built no usable triangles",
                 path.c_str());
        return false;
    }

    TraceLog(LOG_INFO,
             "CollisionMeshLoader: '%s' — %d triangles, %d BVH nodes, %.1f ms"
             "%s",
             path.c_str(), out.getTriangleCount(), out.getNodeCount(), build_ms,
             out.getDegenerateCount() > 0 ? " (degenerate triangles dropped)"
                                          : "");
    if (out.getDegenerateCount() > 0) {
        TraceLog(LOG_WARNING,
                 "CollisionMeshLoader: dropped %d degenerate triangles from "
                 "'%s'",
                 out.getDegenerateCount(), path.c_str());
    }
    return true;
}

#include <Level/Level.h>

#include <nlohmann/json.hpp>
#include <raylib.h>

#include <cstdio>
#include <fstream>
#include <string>

using json = nlohmann::json;

namespace {

/// Schema version written by tools/export_level.py. Bumped only when a change
/// would make an older file load *wrongly* rather than merely incompletely —
/// adding a new optional key does not need it, changing what an existing key
/// means does. Refusing an unknown version is the point: a level that silently
/// half-loads puts collision somewhere the player cannot see.
/// Format 1: collision is BOX_/RAMP_ proxies only.
/// Format 2: adds `collisionMesh`, a triangle soup carrying the geometry those
///           primitives cannot express (curved ground, round towers, arches).
///
/// Both are still read, and that is not just courtesy to old files: a level
/// whose world is simple enough for proxies has no reason to ship a mesh, so
/// greybox and forest legitimately stay at format 1 forever. The exporter
/// writes 2 only when a mesh is actually present, which is what keeps their
/// level.json byte-for-byte unchanged.
constexpr int kMinSupportedFormat = 1;
constexpr int kMaxSupportedFormat = 2;

/// Must stay in step with EnemyType (include/Entities/EnemyFactory.h) and with
/// ENEMY_TYPES in tools/export_level.py. The exporter already rejects unknown
/// names, so reaching the error here means the two tables have drifted.
struct EnemyTypeName {
    const char *name;
    EnemyType type;
};
constexpr EnemyTypeName kEnemyTypes[] = {
    {"Swordman", EnemyType::Swordman},
};

bool parseEnemyType(const std::string &name, EnemyType &out) {
    for (const auto &entry : kEnemyTypes) {
        if (name == entry.name) {
            out = entry.type;
            return true;
        }
    }
    return false;
}

Vector3 toVector3(const json &array) {
    return {array.at(0).get<float>(), array.at(1).get<float>(),
            array.at(2).get<float>()};
}

Color toColor(const json &node) {
    if (!node.is_array() || node.size() < 4) return DARKGRAY;
    return {node.at(0).get<unsigned char>(), node.at(1).get<unsigned char>(),
            node.at(2).get<unsigned char>(), node.at(3).get<unsigned char>()};
}

SpawnPoint toSpawnPoint(const json &node) {
    SpawnPoint spawn;
    spawn.position = toVector3(node.at("position"));
    spawn.yaw = node.value("yaw", 0.0f);
    return spawn;
}

/// Directory part of a path, without the trailing separator. Used to resolve
/// `visualModel` against the JSON that names it, so a level directory can be
/// moved without rewriting what is inside it.
std::string directoryOf(const std::string &path) {
    const size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string(".") : path.substr(0, cut);
}

PhysicsObstacle parseBox(const json &node) {
    const Vector3 centre = toVector3(node.at("center"));
    const Vector3 half = toVector3(node.at("halfExtents"));
    // The constructor takes opposite corners and recovers centre + extents
    // itself, then applies the yaw about that centre.
    return PhysicsObstacle(
        {centre.x - half.x, centre.y - half.y, centre.z - half.z},
        {centre.x + half.x, centre.y + half.y, centre.z + half.z},
        node.value("yaw", 0.0f), toColor(node.value("color", json())));
}

PhysicsObstacle parseRamp(const json &node) {
    const json &min_xz = node.at("minXZ");
    const json &max_xz = node.at("maxXZ");
    return PhysicsObstacle(
        Vector2{min_xz.at(0).get<float>(), min_xz.at(1).get<float>()},
        Vector2{max_xz.at(0).get<float>(), max_xz.at(1).get<float>()},
        node.at("startY").get<float>(), node.at("endY").get<float>(),
        node.value("yaw", 0.0f), toColor(node.value("color", json())));
}

} // namespace

Level LevelLoader::load(const std::string &jsonPath) {
    Level level;

    std::ifstream file(jsonPath);
    if (!file) {
        TraceLog(LOG_ERROR, "LevelLoader: cannot open '%s'", jsonPath.c_str());
        return level;
    }

    json root;
    try {
        // Non-throwing parse would only defer the reporting; the catch below
        // already turns every failure into one logged message plus an invalid
        // Level, which is the contract callers rely on.
        root = json::parse(file);
    } catch (const json::parse_error &err) {
        TraceLog(LOG_ERROR, "LevelLoader: '%s' is not valid JSON: %s",
                 jsonPath.c_str(), err.what());
        return level;
    }

    try {
        const int format = root.value("format", 0);
        if (format < kMinSupportedFormat || format > kMaxSupportedFormat) {
            TraceLog(LOG_ERROR,
                     "LevelLoader: '%s' is format %d, this build reads formats "
                     "%d-%d. Re-export it with tools/export_level.py.",
                     jsonPath.c_str(), format, kMinSupportedFormat,
                     kMaxSupportedFormat);
            return level;
        }

        Level parsed;
        parsed.name = root.value("name", "unnamed");

        const std::string model = root.value("visualModel", std::string());
        if (!model.empty()) {
            parsed.visualModelPath = directoryOf(jsonPath) + "/" + model;
        }

        const std::string mesh = root.value("collisionMesh", std::string());
        if (!mesh.empty()) {
            parsed.collisionMeshPath = directoryOf(jsonPath) + "/" + mesh;
        }

        const json &bounds = root.at("bounds");
        parsed.bounds.min = toVector3(bounds.at("min"));
        parsed.bounds.max = toVector3(bounds.at("max"));

        parsed.playerSpawn = toSpawnPoint(root.at("playerSpawn"));

        for (const json &node : root.value("enemySpawns", json::array())) {
            const std::string type_name = node.at("type").get<std::string>();
            EnemySpawn spawn;
            if (!parseEnemyType(type_name, spawn.type)) {
                TraceLog(LOG_ERROR,
                         "LevelLoader: '%s' spawns unknown enemy type '%s'",
                         jsonPath.c_str(), type_name.c_str());
                return level;
            }
            spawn.at = toSpawnPoint(node);
            parsed.enemySpawns.push_back(spawn);
        }

        const json &obstacles = root.at("obstacles");
        parsed.obstacles.reserve(obstacles.size());
        for (const json &node : obstacles) {
            const std::string type = node.at("type").get<std::string>();
            // `name` is the Blender object's, carried through purely so this
            // message can point at something the author can select and fix.
            const std::string name = node.value("name", "<unnamed>");
            if (type == "box") {
                parsed.obstacles.push_back(parseBox(node));
            } else if (type == "ramp") {
                parsed.obstacles.push_back(parseRamp(node));
            } else {
                TraceLog(LOG_ERROR,
                         "LevelLoader: '%s' obstacle '%s' has unknown type "
                         "'%s'",
                         jsonPath.c_str(), name.c_str(), type.c_str());
                return level;
            }
        }

        if (parsed.obstacles.empty()) {
            TraceLog(LOG_ERROR, "LevelLoader: '%s' has no obstacles",
                     jsonPath.c_str());
            return level;
        }

        parsed.valid = true;
        TraceLog(LOG_INFO,
                 "LevelLoader: '%s' loaded — %d obstacles, %d enemy spawns",
                 parsed.name.c_str(), (int)parsed.obstacles.size(),
                 (int)parsed.enemySpawns.size());
        return parsed;

    } catch (const json::exception &err) {
        TraceLog(LOG_ERROR, "LevelLoader: '%s' is malformed: %s",
                 jsonPath.c_str(), err.what());
        return level;
    }
}

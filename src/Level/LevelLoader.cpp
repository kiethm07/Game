#include <Level/Level.h>

#include <nlohmann/json.hpp>
#include <raylib.h>

#include <cstdio>
#include <filesystem>
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

/// Schema version of assets/levels/<dir>/enemies.json.
///
/// This is the one file in the pipeline a human types rather than a tool emits,
/// which is exactly why it earns a version of its own. Every other file is
/// written by export_level.py, so a stale one cannot exist; an overlay can sit
/// in a level directory for months and meet a build that has moved on.
///
/// The `detailModel` precedent does not transfer. Missing grass is
/// *incomplete*, so an old build reading a newer level is merely plainer. These
/// keys are combat tuning: if `maxHealth` ever became a multiplier, or
/// `startAwareness` moved off the 0-200 scale, every existing overlay would
/// load *wrongly*, with no symptom but a fight that feels off. That is the case
/// a format number exists to refuse.
constexpr int kEnemyOverlayFormat = 1;

const char *const kOverlayFileName = "enemies.json";

/// One spawn out of either source. False means "skip this entry" -- the caller
/// counts it and carries on.
bool parseSpawnEntry(const json &node, const std::string &source, size_t index,
                     EnemySpawn &out) {
    try {
        const std::string type_name = node.at("type").get<std::string>();
        if (!parseEnemyType(type_name, out.type)) {
            TraceLog(LOG_WARNING,
                     "LevelLoader: %s entry %d names enemy type '%s', which is "
                     "not in EnemyTypes.def. Skipped.",
                     source.c_str(), (int)index, type_name.c_str());
            return false;
        }

        // Two shapes, one parser. level.json writes `position: [x, y, z]`
        // because a tool emits it; enemies.json writes flat x/y/z because a
        // person types it and has to be able to leave `y` out -- which a
        // three-element array cannot express without a null.
        if (node.contains("position")) {
            out.at = toSpawnPoint(node);
            out.hasExplicitY = true;
        } else {
            out.at.position.x = node.at("x").get<float>();
            out.at.position.z = node.at("z").get<float>();
            out.hasExplicitY = node.contains("y");
            out.at.position.y = out.hasExplicitY ? node.at("y").get<float>() : 0.0f;
            out.at.yaw = node.value("yaw", 0.0f);
        }

        if (node.contains("maxHealth")) {
            const float health = node.at("maxHealth").get<float>();
            if (health <= 0.0f) {
                TraceLog(LOG_WARNING,
                         "LevelLoader: %s entry %d has maxHealth %.1f, which "
                         "would spawn something already dead. Skipped.",
                         source.c_str(), (int)index, health);
                return false;
            }
            out.overrides.maxHealth = health;
        }
        if (node.contains("vision")) {
            const json &vision = node.at("vision");
            if (vision.contains("radius"))
                out.overrides.visionRadius = vision.at("radius").get<float>();
            if (vision.contains("cone"))
                out.overrides.visionConeDegrees = vision.at("cone").get<float>();
        }
        if (node.contains("startAwareness")) {
            out.overrides.startAwareness =
                node.at("startAwareness").get<float>();
        }
        return true;
    } catch (const json::exception &err) {
        TraceLog(LOG_WARNING, "LevelLoader: %s entry %d is malformed (%s). "
                              "Skipped.",
                 source.c_str(), (int)index, err.what());
        return false;
    }
}

/// Read assets/levels/<dir>/enemies.json, if there is one.
///
/// Found by convention rather than named by a key in level.json, and that is
/// deliberate: a key would have to be written by export_level.py, and the
/// exporter is the one component that provably cannot know whether an overlay
/// exists -- it rebuilds level.json from bpy.data and never reads its own
/// output directory. Convention is what makes the overlay unclobberable.
///
/// On success the overlay *replaces* level.json's spawns entirely, including
/// when it holds an empty list: an empty `spawns` array is an authored
/// statement that this level has no enemies, not an absence to fall back from.
void applyEnemyOverlay(const std::string &dir, Level &parsed) {
    const std::string path = dir + "/" + kOverlayFileName;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;  // The normal case for a level that never needed one.
    }

    const size_t marker_count = parsed.enemySpawns.size();

    std::ifstream file(path);
    json root;
    try {
        if (!file) throw std::runtime_error("cannot open the file");
        root = json::parse(file);
        const int format = root.value("format", 0);
        if (format != kEnemyOverlayFormat) {
            throw std::runtime_error("format " + std::to_string(format) +
                                     ", this build reads format " +
                                     std::to_string(kEnemyOverlayFormat));
        }
        if (!root.contains("spawns") || !root.at("spawns").is_array()) {
            throw std::runtime_error("no \"spawns\" array");
        }
    } catch (const std::exception &err) {
        // The level still loads in full -- geometry, collision, player spawn.
        // What it does NOT do is fall back to the markers: that would resurrect
        // an enemy set the author deliberately replaced, while the level looked
        // fine. Nothing fabricated, nothing resurrected, and loud.
        parsed.enemySpawns.clear();
        parsed.enemyOverlayError =
            std::string(kOverlayFileName) + ": " + err.what();
        TraceLog(LOG_ERROR,
                 "LevelLoader: '%s' could not be read (%s). The level loads "
                 "with NO enemies -- it deliberately does not fall back to the "
                 "%d marker spawns, because reviving a set that was explicitly "
                 "replaced looks correct and is not.",
                 path.c_str(), err.what(), (int)marker_count);
        return;
    }

    std::vector<EnemySpawn> spawns;
    int skipped = 0;
    for (const json &node : root.at("spawns")) {
        EnemySpawn spawn;
        if (!parseSpawnEntry(node, path, spawns.size(), spawn)) {
            ++skipped;
            continue;
        }
        spawns.push_back(spawn);
    }

    parsed.enemySpawns = std::move(spawns);
    parsed.enemiesFromOverlay = true;
    TraceLog(LOG_INFO,
             "LevelLoader: '%s' replaces %d marker spawn(s) with %d entry/ies%s",
             path.c_str(), (int)marker_count, (int)parsed.enemySpawns.size(),
             skipped ? TextFormat(" (%d skipped)", skipped) : "");
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

        // Optional, and read with value() rather than at() for the reason the
        // header states: a level that predates the key loads without scenery,
        // which is incomplete rather than wrong, so it needs no format bump.
        const std::string detail = root.value("detailModel", std::string());
        if (!detail.empty()) {
            parsed.detailModelPath = directoryOf(jsonPath) + "/" + detail;
        }

        const json &bounds = root.at("bounds");
        parsed.bounds.min = toVector3(bounds.at("min"));
        parsed.bounds.max = toVector3(bounds.at("max"));

        parsed.playerSpawn = toSpawnPoint(root.at("playerSpawn"));

        // A bad entry skips itself rather than failing the level.
        //
        // This used to `return level;`, so one typo cost the whole map -- and
        // an invalid Level is not "the level minus an enemy", it is zero
        // obstacles and the player in freefall. The all-or-nothing rule that
        // governs collision turns on the word *silently*: a missing wall is
        // undetectable until someone walks through it in a shipped build,
        // while a missing enemy is something the author is standing in the
        // level looking for, with the log open. Different detectability,
        // different policy.
        int skipped = 0;
        for (const json &node : root.value("enemySpawns", json::array())) {
            EnemySpawn spawn;
            if (!parseSpawnEntry(node, jsonPath, parsed.enemySpawns.size(),
                                 spawn)) {
                ++skipped;
                continue;
            }
            parsed.enemySpawns.push_back(spawn);
        }
        if (skipped > 0) {
            TraceLog(LOG_WARNING,
                     "LevelLoader: '%s' -- %d of %d marker spawns skipped, %d "
                     "kept.",
                     jsonPath.c_str(), skipped,
                     skipped + (int)parsed.enemySpawns.size(),
                     (int)parsed.enemySpawns.size());
        }

        // After the marker spawns, because it replaces them.
        applyEnemyOverlay(directoryOf(jsonPath), parsed);

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

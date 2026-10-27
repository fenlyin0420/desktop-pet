#include "config.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "json.hpp"

namespace fx {

static std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

static bool exists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

static bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static void mkdirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] == '/' && cur.size() > 1) ::mkdir(cur.c_str(), 0755);
    }
    ::mkdir(path.c_str(), 0755);
}

std::string exeDir() {
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = '\0';
    std::string p(buf);
    auto slash = p.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}

static std::string homeDir() {
    const char* h = std::getenv("HOME");
    return (h && *h) ? std::string(h) : std::string(".");
}

static std::vector<std::string> candidateAssetDirs(const std::string& base) {
    return {
        envOr("FXPET_ASSETS", ""),
        base + "/assets",
        base + "/assets/img/..",
        base,
        std::string("assets"),
        base + "/../assets",
    };
}

void resolvePaths(Config& cfg) {
    cfg.baseDir = exeDir();
    cfg.assetDir.clear();
    for (const std::string& cand : candidateAssetDirs(cfg.baseDir)) {
        if (cand.empty()) continue;
        std::string dir = cand;
        if (dir.size() > 1 && dir.back() == '/') dir.pop_back();
        if (isDir(dir + "/img")) {
            cfg.assetDir = dir;
            break;
        }
    }
    if (cfg.assetDir.empty()) cfg.assetDir = cfg.baseDir + "/assets";

    cfg.configPath = envOr("FXPET_CONFIG", cfg.assetDir + "/config.json");
    if (!exists(cfg.configPath) && exists(cfg.baseDir + "/config.json")) cfg.configPath = cfg.baseDir + "/config.json";

    const std::string cfgHome = envOr("XDG_CONFIG_HOME", homeDir() + "/.config") + "/fxpet";
    mkdirs(cfgHome);
    cfg.statePath = cfgHome + "/state.ini";

    const std::string stateHome = envOr("XDG_STATE_HOME", homeDir() + "/.local/state") + "/fxpet";
    mkdirs(stateHome);
    cfg.logPath = stateHome + "/fxpet.log";
}

void logLine(const char* fmt, ...) {
    static std::string path;
    if (path.empty()) {
        const char* h = std::getenv("HOME");
        path = std::string((h && *h) ? h : ".") + "/.local/state/fxpet";
        mkdirs(path);
        path += "/fxpet.log";
    }
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    char tbuf[32];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tmv);
    std::fprintf(f, "[%s] ", tbuf);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

static std::vector<KeyGroup> readKeyGroups(const json::Value& v) {
    std::vector<KeyGroup> out;
    if (!v.isArray()) return out;
    for (size_t i = 0; i < v.size(); ++i) {
        const json::Value& entry = v.at(i);
        KeyGroup g;
        if (entry.isArray()) {
            for (size_t k = 0; k < entry.size(); ++k) g.push_back(entry.at(k).integer());
        } else if (entry.type() == json::Value::Type::Number) {
            g.push_back(entry.integer());
        }
        out.push_back(g);
    }
    return out;
}

static Vec2f readVec2(const json::Value& v, Vec2f def) {
    if (!v.isArray() || v.size() < 2) return def;
    return Vec2f{static_cast<float>(v.at(0).num(def.x)), static_cast<float>(v.at(1).num(def.y))};
}

bool loadConfig(Config& cfg, std::string* error) {
    // 与 Windows 版一致的默认键位表（VK 码）
    if (cfg.standardHand.empty()) {
        cfg.standardHand = {{9}, {49}, {50}, {51}, {52}, {16}, {81}, {87}, {69}, {82}, {32}, {65}, {83}, {68}, {70}};
    }
    if (cfg.mouseLeft.empty()) cfg.mouseLeft = {{1}};
    if (cfg.mouseRight.empty()) cfg.mouseRight = {{2}};
    if (cfg.mouseSide.empty()) cfg.mouseSide = {{5}, {6}};

    json::Value root;
    if (!json::loadFile(cfg.configPath, root, error)) return false;
    if (!root.isObject()) return false;

    const json::Value& dec = root["decoration"];
    if (dec.isObject()) {
        cfg.windowW = dec["window_size"].at(0).integer(cfg.windowW);
        cfg.windowH = dec["window_size"].at(1).integer(cfg.windowH);
        cfg.scalarX = static_cast<float>(dec["scalar"].at(0).num(1.0));
        cfg.scalarY = static_cast<float>(dec["scalar"].at(1).num(1.0));
        cfg.framerate = dec["framerateLimit"].integer(cfg.framerate);
        cfg.topWindow = dec["topWindow"].boolean(true);
        cfg.leftHanded = dec["leftHanded"].boolean(true);
        // 注意：Windows 版 l2d_correct / l2d_horizontal_flip 的语义与这里不同，
        // Linux 版改用 linux.l2d_scale / linux.l2d_flip_x（见 README）。
        cfg.l2dOffset = readVec2(dec["l2d_offset"], cfg.l2dOffset);
        cfg.handOffset = readVec2(dec["hand_offset"], cfg.handOffset);
        cfg.mouseForceMove = dec["mouse_force_move"].boolean(true);
        cfg.mouseSpeed = static_cast<float>(dec["mouse_speed"].num(1.0));
    }
    if (cfg.scalarX <= 0.05f) cfg.scalarX = 1.0f;
    if (cfg.scalarY <= 0.05f) cfg.scalarY = 1.0f;
    if (cfg.windowW < 64) cfg.windowW = 612;
    if (cfg.windowH < 64) cfg.windowH = 354;
    if (cfg.framerate < 5 || cfg.framerate > 240) cfg.framerate = 60;

    const json::Value& std_ = root["standard"];
    if (std_.isObject()) {
        if (std_.has("l2d")) cfg.standardL2d = std_["l2d"].boolean(true);
        if (std_.has("mouse")) cfg.standardMouse = std_["mouse"].boolean(true);
        if (std_["hand"].isArray()) cfg.standardHand = readKeyGroups(std_["hand"]);
        if (std_["face"].isArray()) cfg.standardFace = readKeyGroups(std_["face"]);
        if (std_["mouse_left"].isArray() && std_["mouse_left"].size()) cfg.mouseLeft = readKeyGroups(std_["mouse_left"]);
        if (std_["mouse_right"].isArray() && std_["mouse_right"].size()) cfg.mouseRight = readKeyGroups(std_["mouse_right"]);
        if (std_["mouse_side"].isArray() && std_["mouse_side"].size()) cfg.mouseSide = readKeyGroups(std_["mouse_side"]);
    }
    if (root.has("mode")) cfg.mode = root["mode"].integer(0);

    // 可选：Linux 版专用覆盖项
    const json::Value& linuxCfg = root["linux"];
    if (linuxCfg.isObject()) {
        cfg.l2dCorrect = static_cast<float>(linuxCfg["l2d_scale"].num(1.0));
        cfg.l2dFlipX = linuxCfg["l2d_flip_x"].boolean(false);
        if (cfg.l2dCorrect <= 0.05f) cfg.l2dCorrect = 1.0f;
        if (linuxCfg.has("watermark_param")) cfg.watermarkParam = linuxCfg["watermark_param"].str();
        cfg.hideWatermark = linuxCfg["hide_watermark"].boolean(true);
        if (linuxCfg.has("hand_part")) cfg.handPart = linuxCfg["hand_part"].str();
        if (linuxCfg.has("hand_drawables")) {
            cfg.handDrawables.clear();
            const std::string list = linuxCfg["hand_drawables"].str();
            size_t start = 0;
            while (start < list.size()) {
                const size_t comma = list.find(',', start);
                const std::string piece = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!piece.empty()) cfg.handDrawables.push_back(std::atoi(piece.c_str()));
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
        cfg.watermarkOn = static_cast<float>(linuxCfg["watermark_on"].num(cfg.watermarkOn));
        cfg.watermarkOff = static_cast<float>(linuxCfg["watermark_off"].num(cfg.watermarkOff));
    }
    return true;
}

bool loadState(Config& cfg, State& st) {
    (void)cfg;
    FILE* f = std::fopen(cfg.statePath.c_str(), "r");
    if (!f) return false;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        int a, b, c, d;
        if (std::sscanf(line, "window=%d,%d,%d,%d", &a, &b, &c, &d) == 4) {
            st.window = Rect{a, b, c, d};
        } else if (std::sscanf(line, "locked=%d", &a) == 1) {
            st.locked = a != 0;
        } else if (std::sscanf(line, "visible=%d", &a) == 1) {
            st.visible = a != 0;
        }
    }
    std::fclose(f);
    return true;
}

bool saveState(const Config& cfg, const State& st) {
    FILE* f = std::fopen(cfg.statePath.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "window=%d,%d,%d,%d\n", st.window.x, st.window.y, st.window.w, st.window.h);
    std::fprintf(f, "locked=%d\n", st.locked ? 1 : 0);
    std::fprintf(f, "visible=%d\n", st.visible ? 1 : 0);
    std::fclose(f);
    return true;
}

}  // namespace fx

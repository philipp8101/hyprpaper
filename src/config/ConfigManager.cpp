#include "ConfigManager.hpp"
#include "../Hyprpaper.hpp"
#include <hyprutils/path/Path.hpp>
#include <filesystem>
#include <random>
#include <ranges>
#include <magic.h>

static Hyprlang::CParseResult handleWallpaper(const char* C, const char* V) {
    const std::string COMMAND = C;
    const std::string VALUE = V;
    Hyprlang::CParseResult result;

    if (VALUE.find_first_of(',') == std::string::npos) {
        result.setError("wallpaper failed (syntax)");
        return result;
    }

    auto MONITOR   = VALUE.substr(0, VALUE.find_first_of(','));
    auto WALLPAPER = g_pConfigManager->trimPath(VALUE.substr(VALUE.find_first_of(',') + 1));

    bool contain = false;

    if (WALLPAPER.find("contain:") == 0) {
        WALLPAPER = WALLPAPER.substr(8);
        contain   = true;
    }

    if (WALLPAPER[0] == '~') {
        static const char* const ENVHOME = getenv("HOME");
        WALLPAPER                        = std::string(ENVHOME) + WALLPAPER.substr(1);
    }

    std::error_code ec;

    // if (!std::filesystem::exists(WALLPAPER, ec)) {
    //     result.setError((std::string{"wallpaper failed ("} + (ec ? ec.message() : std::string{"no such file"}) + std::string{": "} + WALLPAPER + std::string{")"}).c_str());
    //     return result;
    // }

    auto sWallpaper = (SWallpaperSource){ .uid = WALLPAPER, .path = WALLPAPER};
    if (std::find(g_pConfigManager->m_dRequestedPreloads.begin(), g_pConfigManager->m_dRequestedPreloads.end(), sWallpaper) == g_pConfigManager->m_dRequestedPreloads.end() &&
        !g_pHyprpaper->isPreloaded(sWallpaper)) {
        result.setError("wallpaper failed (not preloaded)");
        return result;
    }

    g_pHyprpaper->clearWallpaperFromMonitor(MONITOR);
    g_pHyprpaper->m_mMonitorActiveWallpapers[MONITOR]            = sWallpaper;
    g_pHyprpaper->m_mMonitorWallpaperRenderData[MONITOR].contain = contain;

    if (MONITOR.empty()) {
        for (auto& m : g_pHyprpaper->m_vMonitors) {
            if (!m->hasATarget || m->wildcard) {
                g_pHyprpaper->clearWallpaperFromMonitor(m->name);
                g_pHyprpaper->m_mMonitorActiveWallpapers[m->name]            = sWallpaper;
                g_pHyprpaper->m_mMonitorWallpaperRenderData[m->name].contain = contain;
            }
        }
    } else {
        const auto PMON = g_pHyprpaper->getMonitorFromName(MONITOR);
        if (PMON)
            PMON->wildcard = false;
    }

    return result;
}

static Hyprlang::CParseResult handlePreload(const char* C, const char* V) {
    const std::string COMMAND   = C;
    const std::string VALUE     = V;
    auto              WALLPAPER = VALUE;

    if (WALLPAPER[0] == '~') {
        static const char* const ENVHOME = getenv("HOME");
        WALLPAPER                        = std::string(ENVHOME) + WALLPAPER.substr(1);
    }

    std::error_code ec;

    if (!std::filesystem::exists(WALLPAPER, ec)) {
        Hyprlang::CParseResult result;
        result.setError(((ec ? ec.message() : std::string{"no such file"}) + std::string{": "} + WALLPAPER).c_str());
        return result;
    }

    g_pConfigManager->m_dRequestedPreloads.emplace_back((SWallpaperSource){ .uid = WALLPAPER, .path = WALLPAPER });

    return Hyprlang::CParseResult{};
}

static Hyprlang::CParseResult handlePreloadRandom(const char* C, const char* V) {
    const std::string COMMAND   = C;
    const std::string VALUE     = V;

    const auto       BEGINLOAD = std::chrono::system_clock::now();

    if (VALUE.find_first_of(',') == std::string::npos) {
        Hyprlang::CParseResult result;
        result.setError("wallpaper failed (syntax)");
        return result;
    }

    auto PATH = g_pConfigManager->trimPath(VALUE.substr(0, VALUE.find_first_of(',')));
    std::vector<std::string> uids;
    std::stringstream stream(VALUE.substr(VALUE.find_first_of(',') + 1));
    std::string item;
    while (std::getline(stream, item, ',')) {
        uids.push_back(g_pConfigManager->trimPath(item));
    }

    if (PATH[0] == '~') {
        static const char* const ENVHOME = getenv("HOME");
        PATH = std::string(ENVHOME) + PATH.substr(1);
    }

    std::error_code ec;

    if (!std::filesystem::exists(PATH, ec)) {
        Hyprlang::CParseResult result;
        result.setError(((ec ? ec.message() : std::string{"no such file"}) + std::string{": "} + PATH).c_str());
        return result;
    }

    if (!std::filesystem::is_directory(PATH, ec)) {
        Hyprlang::CParseResult result;
        result.setError(((ec ? ec.message() : std::string{"path must be a directory"}) + std::string{": "} + PATH).c_str());
        return result;
    }
    auto filter = std::ranges::views::filter([](const std::filesystem::directory_entry& e) {
            if (!(e.is_regular_file() || e.is_symlink()))
                return false;
            auto file = e.is_symlink() ? e.path().parent_path() / std::filesystem::read_symlink(e.path()) : e.path();
            magic_t magic_cookie;
            magic_cookie = magic_open(MAGIC_MIME_TYPE);
            if (magic_cookie == NULL) 
                return false;
            if (magic_load(magic_cookie, NULL) != 0)
                return false;
            const char *mime_type = magic_file(magic_cookie, file.c_str());
            if (mime_type == NULL)
                return false;
            bool ret = std::string(mime_type).find("image") != std::string::npos;
            magic_close(magic_cookie);
            return ret;
            });
    auto filter_size = 0;
    for (auto _ : std::filesystem::directory_iterator(PATH)|filter) {
        filter_size++;
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::vector<size_t> full_range(filter_size);
    std::iota(full_range.begin(), full_range.end(), 0);
    std::shuffle(full_range.begin(), full_range.end(), gen);
    std::vector<size_t> random_idx(full_range.begin(), std::next(full_range.begin(), uids.size()));
    size_t idx = 0;
    std::vector<std::optional<std::filesystem::directory_entry>> paths;
    for (auto &e : std::filesystem::directory_iterator(PATH)|filter) {
        if (std::find(random_idx.begin(), random_idx.end(), idx) != random_idx.end()) {
            paths.push_back(e);
        }
        idx++;
    }
    if (paths.empty()) {
        Hyprlang::CParseResult result;
        result.setError(std::format("could not find any valid file in directory: {}", PATH).c_str());
        return result;
    }
    if (paths.size() < uids.size()) {
        Hyprlang::CParseResult result;
        result.setError(std::format("could not find enough valid files in directory: {} found {} requested {}", PATH, paths.size(), uids.size()).c_str());
        return result;
    }
    for (size_t i = 0; i < paths.size(); i++) {
        g_pConfigManager->m_dRequestedPreloads.emplace_back((SWallpaperSource){ .uid = uids[i], .path = paths[i]->path() });
    }

    const auto MS = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - BEGINLOAD).count() / 1000.f;
    Debug::log(LOG, "Selected %d random images from %s in %dms", paths.size(), PATH.c_str(), MS);

    return Hyprlang::CParseResult{};
}

static Hyprlang::CParseResult handleUnloadAll(const char* C, const char* V) {
    const std::string        COMMAND = C;
    const std::string        VALUE   = V;
    std::vector<SWallpaperSource> toUnload;

    for (auto& [name, target] : g_pHyprpaper->m_mWallpaperTargets) {
        if (VALUE == "unused") {
            bool exists = false;
            for (auto& [mon, target2] : g_pHyprpaper->m_mMonitorActiveWallpaperTargets) {
                if (&target == target2) {
                    exists = true;
                    break;
                }
            }

            if (exists)
                continue;
        }

        toUnload.emplace_back(name);
    }

    for (auto& tu : toUnload)
        g_pHyprpaper->unloadWallpaper(tu);

    return Hyprlang::CParseResult{};
}

static Hyprlang::CParseResult handleUnload(const char* C, const char* V) {
    const std::string COMMAND   = C;
    const std::string VALUE     = V;
    auto              WALLPAPER = VALUE;

    if (VALUE == "all" || VALUE == "unused")
        return handleUnloadAll(C, V);

    if (WALLPAPER[0] == '~') {
        static const char* const ENVHOME = getenv("HOME");
        WALLPAPER                        = std::string(ENVHOME) + WALLPAPER.substr(1);
    }

    g_pHyprpaper->unloadWallpaper((SWallpaperSource){ .uid = WALLPAPER, .path = WALLPAPER});

    return Hyprlang::CParseResult{};
}

static Hyprlang::CParseResult handleReload(const char* C, const char* V) {
    const std::string COMMAND = C;
    const std::string VALUE   = V;

    auto              WALLPAPER = g_pConfigManager->trimPath(VALUE.substr(VALUE.find_first_of(',') + 1));

    if (WALLPAPER.find("contain:") == 0) {
        WALLPAPER = WALLPAPER.substr(8);
    }

    auto preloadResult = handlePreload(C, WALLPAPER.c_str());
    if (preloadResult.error)
        return preloadResult;

    auto MONITOR = VALUE.substr(0, VALUE.find_first_of(','));

    if (MONITOR.empty()) {
        for (auto& m : g_pHyprpaper->m_vMonitors) {
            auto OLD_WALLPAPER = g_pHyprpaper->m_mMonitorActiveWallpapers[m->name];
            g_pHyprpaper->unloadWallpaper(OLD_WALLPAPER);
        }
    } else {
        auto OLD_WALLPAPER = g_pHyprpaper->m_mMonitorActiveWallpapers[MONITOR];
        g_pHyprpaper->unloadWallpaper(OLD_WALLPAPER);
    }

    auto wallpaperResult = handleWallpaper(C, V);
    if (wallpaperResult.error)
        return wallpaperResult;

    return Hyprlang::CParseResult{};
}

CConfigManager::CConfigManager() {
    // Initialize the configuration
    // Read file from default location
    // or from an explicit location given by user

    std::string configPath = getMainConfigPath();

    config = std::make_unique<Hyprlang::CConfig>(configPath.c_str(), Hyprlang::SConfigOptions{.allowMissingConfig = true});

    config->addConfigValue("ipc", Hyprlang::INT{1L});
    config->addConfigValue("splash", Hyprlang::INT{0L});
    config->addConfigValue("splash_offset", Hyprlang::FLOAT{2.F});
    config->addConfigValue("splash_color", Hyprlang::INT{0x55ffffff});

    config->registerHandler(&handleWallpaper, "wallpaper", {.allowFlags = false});
    config->registerHandler(&handleUnload, "unload", {.allowFlags = false});
    config->registerHandler(&handlePreload, "preload", {.allowFlags = false});
    config->registerHandler(&handlePreloadRandom, "preload-random", {.allowFlags = false});
    config->registerHandler(&handleUnloadAll, "unloadAll", {.allowFlags = false});
    config->registerHandler(&handleReload, "reload", {.allowFlags = false});

    config->commence();
}

void CConfigManager::parse() {
    const auto ERROR = config->parse();

    if (ERROR.error)
        std::cout << "Error in config: \n" << ERROR.getError() << "\n";
}

std::string CConfigManager::getMainConfigPath() {
    if (!g_pHyprpaper->m_szExplicitConfigPath.empty())
        return g_pHyprpaper->m_szExplicitConfigPath;

    static const auto paths = Hyprutils::Path::findConfig("hyprpaper");
    if (paths.first.has_value())
        return paths.first.value();
    else
        throw std::runtime_error("Could not find config in HOME, XDG_CONFIG_HOME, XDG_CONFIG_DIRS or /etc/hypr.");
}

// trim from both ends
std::string CConfigManager::trimPath(std::string path) {
    if (path.empty())
        return "";

    // trims whitespaces, tabs and new line feeds
    size_t pathStartIndex = path.find_first_not_of(" \t\r\n");
    size_t pathEndIndex   = path.find_last_not_of(" \t\r\n");
    return path.substr(pathStartIndex, pathEndIndex - pathStartIndex + 1);
}

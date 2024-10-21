#pragma once
#include "../defines.hpp"
#include <hyprlang.hpp>
#include <filesystem>
#include <functional>

struct SWallpaperSource {
    std::string uid;
    std::filesystem::path path;
    bool operator==(const SWallpaperSource& other) const {
        return uid == other.uid;
    }
};


namespace std {
    template <>
    struct hash<SWallpaperSource> {
        size_t operator()(const SWallpaperSource& myStruct) const {
            return hash<std::string>()(myStruct.uid);
        }
    };
}

class CIPCSocket;

class CConfigManager {
  public:
    // gets all the data from the config
    CConfigManager();
    void                               parse();

    std::deque<SWallpaperSource>       m_dRequestedPreloads;
    std::string                        getMainConfigPath();
    std::string                        trimPath(std::string path);

    std::unique_ptr<Hyprlang::CConfig> config;

  private:
    friend class CIPCSocket;
};

inline std::unique_ptr<CConfigManager> g_pConfigManager;

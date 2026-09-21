#pragma once

#include <cstddef>
#include <cstdint>

#include "ConfigFileOperation.h"
#include <MemoryAllocation/UniquePtr.h>
#include <Platform/PlatformPath.h>

#include "ConfigVariables.h"

struct ConfigState {
    bool autoSaveScheduled{false};
    bool loadScheduled{false};
    ConfigFileOperation currentFileOperation{ConfigFileOperation::None};
    char8_t* fileOperationBuffer{};
    std::size_t bufferUsedBytes{};
    UniquePtr<platform::PathCharType[]> pathToConfigDirectory{};
    UniquePtr<platform::PathCharType[]> pathToConfigFile{};
    UniquePtr<platform::PathCharType[]> pathToConfigTempFile{};
    ConfigVariables configVariables{};

    // The ACTIVE config = what autosave writes and what the navbar dropdown shows. Switching
    // configs rebuilds the file paths; values in memory stay until the load replaces them.
    static constexpr std::size_t kMaxConfigNameLength = 40;   // file name without the ".cfg"
    char8_t activeConfigName[kMaxConfigNameLength + 5]{};     // + ".cfg" + NUL

    // Snapshot of "<configDir>/*.cfg" for the navbar dropdown, published to JS as attributes.
    static constexpr std::uint8_t kMaxListedConfigs = 16;
    static constexpr std::size_t kMaxListedNameLength = kMaxConfigNameLength + 4; // with ".cfg"
    std::uint8_t listedConfigCount{0};
    char8_t listedConfigs[kMaxListedConfigs][kMaxListedNameLength + 1]{};
    bool configListDirty{true};
};

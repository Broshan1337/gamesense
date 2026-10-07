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

    
    
    static constexpr std::size_t kMaxConfigNameLength = 40;   
    char8_t activeConfigName[kMaxConfigNameLength + 5]{};     

    
    static constexpr std::uint8_t kMaxListedConfigs = 16;
    static constexpr std::size_t kMaxListedNameLength = kMaxConfigNameLength + 4; 
    std::uint8_t listedConfigCount{0};
    char8_t listedConfigs[kMaxListedConfigs][kMaxListedNameLength + 1]{};
    bool configListDirty{true};
};

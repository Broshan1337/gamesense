#pragma once

#include <algorithm>
#include <cassert>
#include <cstring>
#include <BuildConfig.h>
#include <MemoryAllocation/UniquePtr.h>
#include <Platform/PlatformPath.h>

#include "ConfigFileOperation.h"
#include "ConfigFromString.h"
#include "ConfigDocument.h"
#include "ConfigSchema.h"
#include "ConfigState.h"
#include "ConfigStringConversionState.h"
#include "ConfigToString.h"
#include "ConfigVariableChangeHandler.h"

#if IS_WIN64()
#include <Platform/Windows/FileSystem/WindowsFileSystem.h>
#include <Platform/Macros/PlatformSpecific.h>
#include <Utils/Wcslen.h>
#elif IS_LINUX()
#include <Platform/Linux/LinuxPlatformApi.h>
#endif

template <typename HookContext, typename ChangeHandler = ConfigVariableChangeHandler<HookContext>>
class Config {
public:
    explicit Config(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void init() noexcept
    {
        buildConfigDirectoryPath();
        buildConfigFilePath(WIN64_LINUX(L"default.cfg", "default.cfg"));
        buildConfigTempFilePath();
        static constinit char8_t fileOperationBuffer[build::kConfigFileBufferSize];
        state().fileOperationBuffer = fileOperationBuffer;
        setActiveConfigName(WIN64_LINUX(L"default.cfg", "default.cfg"));
    }

    template <typename ConfigVariable>
    [[nodiscard]] auto getVariable() noexcept
    {
        return state().configVariables.template getVariableValue<ConfigVariable>();
    }

    template <typename ConfigVariable>
    bool setVariable(ConfigVariable::ValueType newValue) noexcept
    {
        if (changeVariableValue<ConfigVariable>(newValue)) {
            scheduleAutoSave();
            return true;
        }
        return false;
    }

    template <typename ConfigVariable>
    void setVariableWithoutAutoSave(ConfigVariable::ValueType newValue) noexcept
    {
        if constexpr (hasChangeHandler<ConfigVariable>()) {
            if (getVariable<ConfigVariable>() != newValue)
                invokeChangeHandler<ConfigVariable>(newValue);
        }

        state().configVariables.template storeVariableValue<ConfigVariable>(newValue);
    }

    void restoreDefaults() noexcept
    {
        config_overrides::restore();
        ConfigVariableTypes::forEach([this] <typename ConfigVariable> (std::type_identity<ConfigVariable>) {
            this->setVariableWithoutAutoSave<ConfigVariable>(ConfigVariable::kDefaultValue);
        });
        scheduleAutoSave();
    }

    void scheduleLoad() noexcept
    {
        state().loadScheduled = true;
    }

    
    
    [[nodiscard]] std::uint32_t loadRevision() const noexcept { return state().loadRevision.load(std::memory_order_acquire); }
    [[nodiscard]] bool lastLoadSucceeded() const noexcept { return state().lastLoadSucceeded.load(std::memory_order_relaxed); }

    void saveActive() noexcept
    {
        scheduleAutoSave();
    }

    
    
    
    
    
    

    static constexpr std::uint8_t kMaxListedConfigs = ConfigState::kMaxListedConfigs;

    [[nodiscard]] std::uint8_t listedConfigCount() const noexcept
    {
        return state().listedConfigCount;
    }

    
    [[nodiscard]] const char* listedConfigName(std::uint8_t index) const noexcept
    {
        if (index < state().listedConfigCount)
            return reinterpret_cast<const char*>(state().listedConfigs[index]);
        return "";
    }

    [[nodiscard]] std::uint8_t activeConfigIndex() const noexcept
    {
        for (std::uint8_t i = 0; i < state().listedConfigCount; ++i) {
            if (std::strcmp(reinterpret_cast<const char*>(state().listedConfigs[i]), reinterpret_cast<const char*>(state().activeConfigName)) == 0)
                return i;
        }
        return 0;
    }

    [[nodiscard]] const char* activeConfigNameForDisplay() const noexcept
    {
        return reinterpret_cast<const char*>(state().activeConfigName);
    }

    [[nodiscard]] bool consumeConfigListDirty() noexcept
    {
        return std::exchange(state().configListDirty, false);
    }

    void markConfigListDirty() noexcept
    {
        state().configListDirty = true;
    }

    
    void refreshConfigList() noexcept
    {
        state().listedConfigCount = 0;
        if (!state().pathToConfigDirectory)
            return;

        if (auto* const dir = LinuxPlatformApi::openDir(reinterpret_cast<const char*>(state().pathToConfigDirectory.get()))) {
            while (const auto* entry = LinuxPlatformApi::readDir(dir)) {
                const auto nameLength = std::strlen(entry);
                if (nameLength <= 4 || nameLength > ConfigState::kMaxListedNameLength)
                    continue;
                if (std::memcmp(entry + nameLength - 4, ".cfg", 4) != 0)
                    continue;
                if (state().listedConfigCount >= ConfigState::kMaxListedConfigs)
                    break;
                auto* dest = state().listedConfigs[state().listedConfigCount];
                std::memset(dest, 0, sizeof(state().listedConfigs[0]));
                std::memcpy(dest, entry, nameLength + 1);
                ++state().listedConfigCount;
            }
            LinuxPlatformApi::closeDir(dir);
        }

        
        for (std::uint8_t i = 1; i < state().listedConfigCount; ++i) {
            char8_t buffer[ConfigState::kMaxListedNameLength + 1];
            std::memcpy(buffer, state().listedConfigs[i], sizeof(buffer));
            std::uint8_t j = i;
            while (j > 0 && std::strcmp(reinterpret_cast<const char*>(buffer), listedConfigName(j - 1)) < 0) {
                std::memcpy(state().listedConfigs[j], state().listedConfigs[j - 1], sizeof(buffer));
                --j;
            }
            std::memcpy(state().listedConfigs[j], buffer, sizeof(buffer));
        }

        state().configListDirty = true;
    }

    
    
    
    void switchToConfig(std::uint8_t listedIndex) noexcept
    {
        if (listedIndex >= state().listedConfigCount)
            return;
        const auto* name = state().listedConfigs[listedIndex];
        // Complete the old file operation before changing its paths. Otherwise
        // a queued autosave overwrites the config we are about to load.
        settleFileOperation();
        if (std::strcmp(reinterpret_cast<const char*>(name), reinterpret_cast<const char*>(state().activeConfigName)) != 0
            && state().autoSaveScheduled) {
            state().currentFileOperation = ConfigFileOperation::Save;
            prepareSaveToFile();
            state().autoSaveScheduled = false;
            saveToFile();
        }
        state().autoSaveScheduled = false;
        setActiveConfigName(reinterpret_cast<const platform::PathCharType*>(name));
        rebuildActiveConfigPaths();
        state().loadScheduled = true;
        state().configListDirty = true;   
    }

    
    
    bool deleteListedConfig(std::uint8_t listedIndex) noexcept
    {
        if (listedIndex >= state().listedConfigCount)
            return false;
        const auto* name = reinterpret_cast<const char*>(state().listedConfigs[listedIndex]);
        if (std::strcmp(name, reinterpret_cast<const char*>(state().activeConfigName)) == 0)
            return false;
        const auto path = pathUnderConfigDir(reinterpret_cast<const platform::PathCharType*>(name));
        if (!path)
            return false;
        if (LinuxPlatformApi::unlink(reinterpret_cast<const char*>(path.get())) != 0)
            return false;
        refreshConfigList();
        return true;
    }

    
    
    bool renameActiveConfig(std::string_view requestedName) noexcept
    {
        char newNameWithExt[ConfigState::kMaxConfigNameLength + 5]{};
        std::size_t length = 0;
        for (const char c : requestedName) {
            const bool allowed = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
            if (!allowed || length >= ConfigState::kMaxConfigNameLength)
                return false;
            newNameWithExt[length++] = c;
        }
        if (length == 0)
            return false;
        constexpr std::string_view kExt{".cfg"};
        std::memcpy(newNameWithExt + length, kExt.data(), kExt.size());

        
        if (std::strncmp(newNameWithExt, reinterpret_cast<const char*>(state().activeConfigName), sizeof(state().activeConfigName)) == 0)
            return false;
        for (std::uint8_t i = 0; i < state().listedConfigCount; ++i) {
            if (std::strncmp(newNameWithExt, reinterpret_cast<const char*>(state().listedConfigs[i]), sizeof(state().listedConfigs[i])) == 0)
                return false;
        }

        settleFileOperation();
        if (state().autoSaveScheduled) {
            state().currentFileOperation = ConfigFileOperation::Save;
            prepareSaveToFile();
            state().autoSaveScheduled = false;
            saveToFile();
        }
        const auto oldPath = pathUnderConfigDir(reinterpret_cast<const platform::PathCharType*>(state().activeConfigName));
        const auto newPath = pathUnderConfigDir(std::string_view{newNameWithExt, length + kExt.size()});
        if (!oldPath || !newPath)
            return false;
        if (LinuxPlatformApi::rename(reinterpret_cast<const char*>(oldPath.get()), reinterpret_cast<const char*>(newPath.get())) != 0)
            return false;

        setActiveConfigName(reinterpret_cast<const platform::PathCharType*>(newNameWithExt));
        rebuildActiveConfigPaths();
        refreshConfigList();
        return true;
    }

    
    
    
    
    [[nodiscard]] bool createAndSwitchToConfig(std::string_view requestedName) noexcept
    {
        char8_t sanitized[ConfigState::kMaxConfigNameLength + 1];
        std::size_t length = 0;
        for (const char c : requestedName) {
            const bool allowed = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
            if (!allowed)
                return false;
            if (length >= ConfigState::kMaxConfigNameLength)
                return false;
            sanitized[length++] = static_cast<char8_t>(c);
        }
        if (length == 0)
            return false;

        
        char8_t fileName[ConfigState::kMaxConfigNameLength + 5];
        std::memcpy(fileName, sanitized, length);
        std::memcpy(fileName + length, WIN64_LINUX(L".cfg", ".cfg"), 5);

        for (std::uint8_t i = 0; i < state().listedConfigCount; ++i)
            if (std::strcmp(reinterpret_cast<const char*>(fileName), listedConfigName(i)) == 0)
                return false;
        settleFileOperation();
        if (state().autoSaveScheduled) {
            state().currentFileOperation = ConfigFileOperation::Save;
            prepareSaveToFile();
            state().autoSaveScheduled = false;
            saveToFile();
        }
        state().loadScheduled = false;
        setActiveConfigName(reinterpret_cast<const platform::PathCharType*>(fileName));
        rebuildActiveConfigPaths();
        scheduleAutoSave();   
        state().configListDirty = true;
        return true;
    }

    
    
    
    bool duplicateActiveConfig() noexcept
    {
        settleFileOperation();
        if (state().autoSaveScheduled) {
            state().currentFileOperation = ConfigFileOperation::Save;
            prepareSaveToFile();
            state().autoSaveScheduled = false;
            saveToFile();
        }

        
        
        
        const auto* active = reinterpret_cast<const char*>(state().activeConfigName);
        const auto baseLength = std::strlen(active);
        if (baseLength < 4 || baseLength - 4 > ConfigState::kMaxConfigNameLength - 5)
            return false;

        char8_t copyName[ConfigState::kMaxListedNameLength + 8]{};
        std::memcpy(copyName, state().activeConfigName, baseLength - 4); 
        {
            const auto copyLength = baseLength - 4;
            std::memcpy(copyName + copyLength, "_copy.cfg", 10);
        }

        for (std::uint8_t i = 0; i < state().listedConfigCount; ++i) {
            if (std::strcmp(reinterpret_cast<const char*>(copyName), listedConfigName(i)) == 0)
                return false; 
        }

        const auto sourcePath = pathUnderConfigDir(reinterpret_cast<const platform::PathCharType*>(state().activeConfigName));
        const auto copyPath = pathUnderConfigDir(std::basic_string_view<platform::PathCharType>{reinterpret_cast<const platform::PathCharType*>(copyName), std::strlen(reinterpret_cast<const char*>(copyName))});
        if (!sourcePath || !copyPath)
            return false;

        
        
        const auto buffer = mem::makeUniqueForOverwrite<char[]>(build::kConfigFileBufferSize);
        if (!buffer)
            return false;
        std::size_t bytes{0};
        if (const auto fd = LinuxPlatformApi::open(reinterpret_cast<const char*>(sourcePath.get()), O_RDONLY); fd >= 0) {
            if (const auto read_ = LinuxPlatformApi::pread(fd, buffer.get(), build::kConfigFileBufferSize, 0); read_ > 0)
                bytes = static_cast<std::size_t>(read_);
            LinuxPlatformApi::close(fd);
        }
        if (bytes == 0)
            return false;

        if (const auto fd = LinuxPlatformApi::open(reinterpret_cast<const char*>(copyPath.get()), O_CREAT | O_WRONLY | O_TRUNC, 0666); fd >= 0) {
            std::size_t written{0};
            while (written < bytes) {
                const auto chunk = LinuxPlatformApi::write(fd, buffer.get() + written, bytes - written);
                if (chunk <= 0)
                    break;
                written += static_cast<std::size_t>(chunk);
            }
            LinuxPlatformApi::close(fd);
            if (written != bytes) {
                LinuxPlatformApi::unlink(reinterpret_cast<const char*>(copyPath.get()));
                return false;
            }
        } else {
            return false;
        }

        setActiveConfigName(std::basic_string_view<platform::PathCharType>{reinterpret_cast<const platform::PathCharType*>(copyName), std::strlen(reinterpret_cast<const char*>(copyName))});
        rebuildActiveConfigPaths();
        refreshConfigList();
        return true;
    }

    void update()
    {
        if (state().configListDirty) {
            refreshConfigList();
            state().configListDirty = false;
        }
        switch (state().currentFileOperation) {
        case ConfigFileOperation::None:
            if (state().autoSaveScheduled && !state().loadScheduled) {
                state().currentFileOperation = ConfigFileOperation::Save;
                prepareSaveToFile();
                state().autoSaveScheduled = false;
                break;
            }
            if (state().loadScheduled) {
                state().currentFileOperation = ConfigFileOperation::Load;
                state().bufferUsedBytes = 0;
                state().loadScheduled = false;
                break;
            }
            break;
        case ConfigFileOperation::Load:
            finishLoadFromFile();
            break;
        default:
            break;
        }
    }

    void performFileOperation() noexcept
    {
        switch (state().currentFileOperation) {
        case ConfigFileOperation::Load:
            loadFromFile();
            break;
        case ConfigFileOperation::Save:
            saveToFile();
            break;
        default:
            break;
        }
    }

private:
    void settleFileOperation() noexcept
    {
        if (state().currentFileOperation == ConfigFileOperation::Load) {
            loadFromFile();
            finishLoadFromFile();
        } else if (state().currentFileOperation == ConfigFileOperation::Save) {
            saveToFile();
        }
    }

    template <typename ConfigVariable>
    void invokeChangeHandler(ConfigVariable::ValueType newValue)
    {
        ChangeHandler{hookContext}.onConfigVariableValueChanged(newValue, std::type_identity<ConfigVariable>{});
    }

    template <typename ConfigVariable>
    [[nodiscard]] static constexpr bool hasChangeHandler() noexcept
    {
        return requires {{ ChangeHandler{hookContext}.onConfigVariableValueChanged(ConfigVariable::kDefaultValue, std::type_identity<ConfigVariable>{}) }; };
    }

    template <typename ConfigVariable>
    bool changeVariableValue(ConfigVariable::ValueType newValue) noexcept
    {
        if (getVariable<ConfigVariable>() == newValue)
            return false;

        if constexpr (hasChangeHandler<ConfigVariable>())
            invokeChangeHandler<ConfigVariable>(newValue);

        state().configVariables.template storeVariableValue<ConfigVariable>(newValue);
        return true;
    }

    void scheduleAutoSave() noexcept
    {
        state().autoSaveScheduled = true;
    }

    [[nodiscard]] auto& state()
    {
        return hookContext.configState();
    }

    [[nodiscard]] const auto& state() const
    {
        return hookContext.configState();
    }

    void loadFromFile() noexcept
    {
        if (!state().pathToConfigFile)
            return;

#if IS_WIN64()
        const std::basic_string_view path{state().pathToConfigFile.get(), utils::wcslen(state().pathToConfigFile.get())};
        UNICODE_STRING pathStr{.Length = static_cast<USHORT>(path.length() * sizeof(wchar_t)), .MaximumLength = static_cast<USHORT>(path.length() * sizeof(wchar_t)), .Buffer = const_cast<wchar_t*>(path.data())};
        if (const auto handle = WindowsFileSystem::openFileForReading(pathStr); handle != INVALID_HANDLE_VALUE) {
            state().bufferUsedBytes = WindowsFileSystem::readFile(handle, 0, state().fileOperationBuffer, build::kConfigFileBufferSize);
            WindowsSyscalls::NtClose(handle);
        }
#elif IS_LINUX()
        if (const auto fd = LinuxPlatformApi::open(state().pathToConfigFile.get(), O_RDONLY); fd >= 0) {
            if (const auto read = LinuxPlatformApi::pread(fd, state().fileOperationBuffer, build::kConfigFileBufferSize, 0); read > 0)
                state().bufferUsedBytes = static_cast<std::size_t>(read);
            LinuxPlatformApi::close(fd);
        }
#endif
    }

    void finishLoadFromFile()
    {
        config_overrides::restore();
        assert(state().currentFileOperation == ConfigFileOperation::Load);
        state().currentFileOperation = ConfigFileOperation::None;

        const auto readBytes = state().bufferUsedBytes;
        if (readBytes == 0 || readBytes >= build::kConfigFileBufferSize
            || !config_document::valid(std::span{state().fileOperationBuffer, readBytes})) {
            state().lastLoadSucceeded.store(false, std::memory_order_relaxed);
            state().loadRevision.fetch_add(1, std::memory_order_release);
            state().autoSaveScheduled = false;
            return;
        }
        auto previous = state().configVariables;
        ConfigVariableTypes::forEach([this] <typename Variable> (std::type_identity<Variable>) {
            this->setVariableWithoutAutoSave<Variable>(Variable::kDefaultValue);
        });
        state().autoSaveScheduled = false;
        ConfigStringConversionState conversionState;
        std::size_t parsedBytes{0};
        do {
            assert(conversionState.offset <= readBytes);
            ConfigFromString configFromString{std::span{state().fileOperationBuffer + conversionState.offset, readBytes - conversionState.offset}, conversionState, skipUnknownKeys};
            parsedBytes = ConfigSchema{hookContext}.performConversion(configFromString);
        } while (parsedBytes != 0 && (conversionState.nestingLevel != 0 || conversionState.indexInNestingLevel[0] != 1));
        
        const bool complete = conversionState.nestingLevel == 0 && conversionState.indexInNestingLevel[0] == 1;
        if (!complete) {
            ConfigVariableTypes::forEach([&] <typename Variable> (std::type_identity<Variable>) {
                this->setVariableWithoutAutoSave<Variable>(previous.template getVariableValue<Variable>());
            });
        }
        state().lastLoadSucceeded.store(complete, std::memory_order_relaxed);
        state().loadRevision.fetch_add(1, std::memory_order_release);
    }

    void prepareSaveToFile()
    {
        ConfigStringConversionState conversionState;
        ConfigToString configToString{std::span{state().fileOperationBuffer, build::kConfigFileBufferSize}, conversionState};
        state().bufferUsedBytes = ConfigSchema{hookContext}.performConversion(configToString);
        assert(conversionState.nestingLevel == 0 && conversionState.indexInNestingLevel[0] == 1);
    }

    void saveToFile() noexcept
    {
        assert(state().currentFileOperation == ConfigFileOperation::Save);
        state().currentFileOperation = ConfigFileOperation::None;

        if (!hookContext.osirisDirectoryPath().get() || !state().pathToConfigDirectory || !state().pathToConfigFile || !state().pathToConfigTempFile)
            return;

        const auto numberOfBytesToWrite = state().bufferUsedBytes;
#if IS_WIN64()
        WindowsFileSystem::createDirectory(hookContext.osirisDirectoryPath().get());
        WindowsFileSystem::createDirectory(state().pathToConfigDirectory.get());

        if (const auto handle = WindowsFileSystem::createFileForOverwrite(state().pathToConfigTempFile.get()); handle != INVALID_HANDLE_VALUE) {
            if (WindowsFileSystem::writeFile(handle, 0, state().fileOperationBuffer, numberOfBytesToWrite) == numberOfBytesToWrite)
                WindowsFileSystem::renameFile(handle, state().pathToConfigFile.get());
            WindowsSyscalls::NtClose(handle);
        }
#elif IS_LINUX()
        mkdir(hookContext.osirisDirectoryPath().get(), 0777);
        mkdir(state().pathToConfigDirectory.get(), 0777);

        if (const auto fd = LinuxPlatformApi::open(state().pathToConfigTempFile.get(), O_CREAT | O_WRONLY | O_TRUNC, 0600); fd >= 0) {
            if (std::cmp_equal(LinuxPlatformApi::write(fd, state().fileOperationBuffer, numberOfBytesToWrite), numberOfBytesToWrite))
                rename(state().pathToConfigTempFile.get(), state().pathToConfigFile.get());
            LinuxPlatformApi::close(fd);
        }
#endif
        
        
        state().configListDirty = true;
    }

    void buildConfigDirectoryPath() noexcept
    {
        if (!hookContext.osirisDirectoryPath().get())
            return;

        const std::basic_string_view osirisDirectoryPath{hookContext.osirisDirectoryPath().get(), WIN64_LINUX(utils::wcslen, std::strlen)(hookContext.osirisDirectoryPath().get())};
        constexpr auto kPathSeparatorLength{1};
        constexpr auto kNullTerminatorLength{1};
        const auto length = osirisDirectoryPath.length() + kPathSeparatorLength + build::kConfigDirectoryName.length() + kNullTerminatorLength;
        state().pathToConfigDirectory = mem::makeUniqueForOverwrite<platform::PathCharType[]>(length);
        if (!state().pathToConfigDirectory)
            return;
        std::size_t writeIndex{0};
        std::copy(osirisDirectoryPath.begin(), osirisDirectoryPath.end(), state().pathToConfigDirectory.get() + writeIndex);
        writeIndex += osirisDirectoryPath.length();
        state().pathToConfigDirectory.get()[writeIndex++] = platform::kPathSeparator;
        std::copy(build::kConfigDirectoryName.begin(), build::kConfigDirectoryName.end(), state().pathToConfigDirectory.get() + writeIndex);
        writeIndex += build::kConfigDirectoryName.length();
        state().pathToConfigDirectory.get()[writeIndex++] = 0;
    }

    void buildConfigFilePath(std::basic_string_view<platform::PathCharType> configFileName) noexcept
    {
        state().pathToConfigFile = pathUnderConfigDir(configFileName);
    }

    void buildConfigTempFilePath() noexcept
    {
        if (!state().pathToConfigFile)
            return;

        const std::basic_string_view pathToConfigFile{state().pathToConfigFile.get(), WIN64_LINUX(utils::wcslen, std::strlen)(state().pathToConfigFile.get())};
        const std::basic_string_view configTempFileSuffix{WIN64_LINUX(L".new", ".new")};
        constexpr auto kNullTerminatorLength{1};
        const auto length = pathToConfigFile.length() + configTempFileSuffix.length() + kNullTerminatorLength;
        state().pathToConfigTempFile = mem::makeUniqueForOverwrite<platform::PathCharType[]>(length);
        if (!state().pathToConfigTempFile)
            return;
        std::size_t writeIndex{0};
        std::copy(pathToConfigFile.begin(), pathToConfigFile.end(), state().pathToConfigTempFile.get() + writeIndex);
        writeIndex += pathToConfigFile.length();
        std::copy(configTempFileSuffix.begin(), configTempFileSuffix.end(), state().pathToConfigTempFile.get() + writeIndex);
        writeIndex += configTempFileSuffix.length();
        state().pathToConfigTempFile.get()[writeIndex++] = 0;
    }

    
    [[nodiscard]] UniquePtr<platform::PathCharType[]> pathUnderConfigDir(std::basic_string_view<platform::PathCharType> fileName) noexcept
    {
        if (!state().pathToConfigDirectory)
            return {};

        const std::basic_string_view pathToConfigDirectory{state().pathToConfigDirectory.get(), WIN64_LINUX(utils::wcslen, std::strlen)(state().pathToConfigDirectory.get())};
        constexpr auto kPathSeparatorLength{1};
        constexpr auto kNullTerminatorLength{1};
        const auto length = pathToConfigDirectory.length() + kPathSeparatorLength + fileName.length() + kNullTerminatorLength;
        UniquePtr<platform::PathCharType[]> path{mem::makeUniqueForOverwrite<platform::PathCharType[]>(length)};
        if (!path)
            return {};
        std::size_t writeIndex{0};
        std::copy(pathToConfigDirectory.begin(), pathToConfigDirectory.end(), path.get() + writeIndex);
        writeIndex += pathToConfigDirectory.length();
        path.get()[writeIndex++] = platform::kPathSeparator;
        std::copy(fileName.begin(), fileName.end(), path.get() + writeIndex);
        writeIndex += fileName.length();
        path.get()[writeIndex++] = 0;
        return path;
    }

    void setActiveConfigName(std::basic_string_view<platform::PathCharType> fileName) noexcept
    {
        const auto length = std::min(fileName.length(), ConfigState::kMaxConfigNameLength + 4);
        std::memset(state().activeConfigName, 0, sizeof(state().activeConfigName));
        std::memcpy(state().activeConfigName, fileName.data(), length);
        state().activeConfigName[length] = 0;
    }

    
    
    void rebuildActiveConfigPaths() noexcept
    {
        const auto* name = reinterpret_cast<const char*>(state().activeConfigName);
        const std::basic_string_view<platform::PathCharType> fileNameView{name, std::strlen(name)};
        buildConfigFilePath(fileNameView);
        buildConfigTempFilePath();
    }

    HookContext& hookContext;
};

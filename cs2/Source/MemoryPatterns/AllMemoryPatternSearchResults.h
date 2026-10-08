#pragma once

#include <type_traits>

#include <MemoryPatterns/MemoryPatterns.h>
#include <MemorySearch/PatternSearchResults.h>
#include <Utils/RetAddrSpoofer.h>

struct AllMemoryPatternSearchResults {
    explicit AllMemoryPatternSearchResults(const MemoryPatterns& memoryPatterns)
        : clientPatternSearchResults{memoryPatterns.patternFinders.clientPatternFinder.findPatterns(kClientPatterns)}
        , sceneSystemPatternSearchResults{memoryPatterns.patternFinders.sceneSystemPatternFinder.findPatterns(kSceneSystemPatterns)}
        , tier0PatternSearchResults{memoryPatterns.patternFinders.tier0PatternFinder.findPatterns(kTier0Patterns)}
        , fileSystemPatternSearchResults{memoryPatterns.patternFinders.fileSystemPatternFinder.findPatterns(kFileSystemPatterns)}
        , soundSystemPatternSearchResults{memoryPatterns.patternFinders.soundSystemPatternFinder.findPatterns(kSoundSystemPatterns)}
        , panoramaPatternSearchResults{memoryPatterns.patternFinders.panoramaPatternFinder.findPatterns(kPanoramaPatterns)}
        , schemaSystemPatternSearchResults{memoryPatterns.patternFinders.schemaSystemPatternFinder.findPatterns(kSchemaSystemPatterns)}
    {
    }

    
    
    
    
    
    
    
    template <typename PatternType>
    [[nodiscard]] auto get() const noexcept
    {
        if constexpr (decltype(kClientPatterns)::PatternPool::PatternTypes::template contains<PatternType>())
            return RetAddrSpoofer::wrapSpoofed(clientPatternSearchResults.get<PatternType>());
        else if constexpr (decltype(kSceneSystemPatterns)::PatternPool::PatternTypes::template contains<PatternType>())
            return RetAddrSpoofer::wrapSpoofed(sceneSystemPatternSearchResults.get<PatternType>());
        else if constexpr (decltype(kTier0Patterns)::PatternPool::PatternTypes::template contains<PatternType>())
            return RetAddrSpoofer::wrapSpoofed(tier0PatternSearchResults.get<PatternType>());
        else if constexpr (decltype(kFileSystemPatterns)::PatternPool::PatternTypes::template contains<PatternType>())
            return RetAddrSpoofer::wrapSpoofed(fileSystemPatternSearchResults.get<PatternType>());
        else if constexpr (decltype(kSoundSystemPatterns)::PatternPool::PatternTypes::template contains<PatternType>())
            return RetAddrSpoofer::wrapSpoofed(soundSystemPatternSearchResults.get<PatternType>());
        else if constexpr (decltype(kPanoramaPatterns)::PatternPool::PatternTypes::template contains<PatternType>())
            return RetAddrSpoofer::wrapSpoofed(panoramaPatternSearchResults.get<PatternType>());
        else if constexpr (decltype(kSchemaSystemPatterns)::PatternPool::PatternTypes::template contains<PatternType>())
            return RetAddrSpoofer::wrapSpoofed(schemaSystemPatternSearchResults.get<PatternType>());
    }

    PatternSearchResults<decltype(kClientPatterns)> clientPatternSearchResults;
    PatternSearchResults<decltype(kSceneSystemPatterns)> sceneSystemPatternSearchResults;
    PatternSearchResults<decltype(kTier0Patterns)> tier0PatternSearchResults;
    PatternSearchResults<decltype(kFileSystemPatterns)> fileSystemPatternSearchResults;
    PatternSearchResults<decltype(kSoundSystemPatterns)> soundSystemPatternSearchResults;
    PatternSearchResults<decltype(kPanoramaPatterns)> panoramaPatternSearchResults;
    PatternSearchResults<decltype(kSchemaSystemPatterns)> schemaSystemPatternSearchResults;
};

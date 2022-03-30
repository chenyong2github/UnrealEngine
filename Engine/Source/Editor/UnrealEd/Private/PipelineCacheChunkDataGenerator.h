// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PipelineCacheChunkDataGenerator.h: parts of PSO cache that are only used in the editor
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/IChunkDataGenerator.h"

class ITargetPlatform;

/**
 * Implementation for splitting shader library into chunks when creating streaming install manifests.
 */
class FPipelineCacheChunkDataGenerator : public IChunkDataGenerator
{
	/** As a temporary/transitional feature, allow opting out from chunking per platform. */
	bool bOptedOut;

	/** Temporary/transitional - this holds the platform name whose ini we checked for the opt out. */
	FString PlatformNameUsedForIni;

	/** Shader code library this pipeline cache is associated with. */
	FString ShaderLibraryName;

public:

	/** Target platform passed in MUST match target platform to generate chunks for. This is checked.*/
	FPipelineCacheChunkDataGenerator(const ITargetPlatform* TargetPlatform, const FString& InShaderLibraryName);
	virtual ~FPipelineCacheChunkDataGenerator() = default;

	//~ IChunkDataGenerator
	virtual void GenerateChunkDataFiles(const int32 InChunkId, const TSet<FName>& InPackagesInChunk, const ITargetPlatform* TargetPlatform, FSandboxPlatformFile* InSandboxFile, TArray<FString>& OutChunkFilenames) override;
};

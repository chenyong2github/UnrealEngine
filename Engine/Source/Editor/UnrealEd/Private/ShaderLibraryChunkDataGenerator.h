// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderLibraryChunkDataGenerator.h: parts of shader library that are only used in the editor
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/IChunkDataGenerator.h"

class ITargetPlatform;

/**
 * Implementation for splitting shader library into chunks when creating streaming install manifests.
 */
class FShaderLibraryChunkDataGenerator : public IChunkDataGenerator
{
	/** As a temporary/transitional feature, allow opting out from chunking per platform. */
	bool bOptedOut;

	/** Temporary/transitional - this holds the platform name whose ini we checked for the opt out. */
	FString PlatformNameUsedForIni;

public:

	/** Target platform passed in MUST match target platform to generate chunks for. This is checked.*/
	FShaderLibraryChunkDataGenerator(const ITargetPlatform* TargetPlatform);
	virtual ~FShaderLibraryChunkDataGenerator() = default;

	//~ IChunkDataGenerator
	virtual void GenerateChunkDataFiles(const int32 InChunkId, const TSet<FName>& InPackagesInChunk, const ITargetPlatform* TargetPlatform, FSandboxPlatformFile* InSandboxFile, TArray<FString>& OutChunkFilenames) override;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FSandboxPlatformFile;

/**
 * Interface for generating extra data when creating streaming install manifests.
 * @note See FAssetRegistryGenerator.
 */
class IChunkDataGenerator
{
public:
	virtual ~IChunkDataGenerator() = default;

	/**
	 * Called to generate any additional files that should be added to the given chunk.
	 *
	 * @param InChunkId The ID of the chunk to generate the files for.
	 * @param InPackagesInChunk The set of packages that are in the chunk.
	 * @param InPlatformName The name of the platform this chunk is for.
	 * @param InSandboxFile The sandbox location that staging data for this chunk should be written to.
	 * @param OutChunkFilenames Array of filenames that belong to this chunk (to be appended to).
	 */
	virtual void GenerateChunkDataFiles(const int32 InChunkId, const TSet<FName>& InPackagesInChunk, const FString& InPlatformName, FSandboxPlatformFile* InSandboxFile, TArray<FString>& OutChunkFilenames) = 0;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR	// this has no business existing in a cooked game

#include "CoreMinimal.h"
#include "ShaderCodeLibrary.h"

struct FPipelineCacheFileFormatPSO;

namespace UE
{
namespace PipelineCacheUtilities
{
	/** Describes a particular combination of shaders. */
	struct FPermutation
	{
		/** Each frequency holds an index of shaders in StableArray. */
		int32 Slots[SF_NumFrequencies];
	};

	/** Describes a PSO with an array of other stable shaders that could be used with it. */
	struct FPermsPerPSO
	{
		/** Original PSO (as recorded during the collection run). */
		const FPipelineCacheFileFormatPSO* PSO;
		/** Boolean table describing which frequencies are active (i.e. have valid shaders). */
		bool ActivePerSlot[SF_NumFrequencies];
		/** Array of other stable shaders whose hashes were the same, so they could potentially be used in this PSO. */
		TArray<FPermutation> Permutations;

		FPermsPerPSO()
			: PSO(nullptr)
		{
			for (int32 Index = 0; Index < SF_NumFrequencies; Index++)
			{
				ActivePerSlot[Index] = false;
			}
		}
	};

	/** 
	 * Loads stable shader keys file (using a proprietary format). Stable key is a way to identify a shader independently of its output hash
	 * 
	 * @param Filename filename (with path if needed)
	 * @param InOutArray array to put the file contents. Existing array contents will be preserved and appended to
	 * @return true if successful
	 */
	RENDERCORE_API bool LoadStableKeysFile(const FStringView& Filename, TArray<FStableShaderKeyAndValue>& InOutArray);

	/** 
	 * Saves stable shader keys file (using a proprietary format). Stable key is a way to identify a shader independently of its output hash
	 * 
	 * @param Filename filename (with path if needed)
	 * @param Values values to be saved
	 * @return true if successful
	 */
	RENDERCORE_API bool SaveStableKeysFile(const FStringView& Filename, const TSet<FStableShaderKeyAndValue>& Values);

	/**
	 * Saves stable pipeline cache file.
	 * 
	 * The cache file is saved together with the stable shader keys file that were used to map its hashes to the build-agnostic ("stable") shader identifier.
	 * 
	 * @param OutputFilename file name for the binary file
	 * @param StableResults an array of PSOs together with all permutations allowed for it
	 * @param StableShaderKeyIndexTable the table of build-agnostic shader keys
	 */
	RENDERCORE_API bool SaveStablePipelineCacheFile(const FString& OutputFilename, const TArray<FPermsPerPSO>& StableResults, const TArray<FStableShaderKeyAndValue>& StableShaderKeyIndexTable);

	/**
	 * Loads stable pipeline cache file.
	 * 
	 * @param Filename file to be loaded
	 * @param StableMap Mapping of the stable (build-agnostic) shader keys to the shader code hashes as of the current moment
	 * @param OutPSOs the PSOs loaded from that file
	 * @param OutTargetPlatform target platform for this file
	 * @param OutPSOsRejected number of PSOs that were rejected during loading (usually because the stable key it used is no longer present in StableMap)
	 * @param OutPSOsMerged number of PSOs that mapped to the same shader code hashes despite using different build-agnostic ("stable") shader keys.
	 */
	RENDERCORE_API bool LoadStablePipelineCacheFile(const FString& Filename, const TMultiMap<FStableShaderKeyAndValue, FSHAHash>& StableMap, TSet<FPipelineCacheFileFormatPSO>& OutPSOs, FName& OutTargetPlatform, int32& OutPSOsRejected, int32& OutPSOsMerged);
}
};

#endif // WITH_EDITOR

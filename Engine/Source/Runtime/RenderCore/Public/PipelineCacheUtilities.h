// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR	// this has no business existing in a cooked game

#include "CoreMinimal.h"
#include "ShaderCodeLibrary.h"

namespace UE
{
namespace PipelineCacheUtilities
{
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
}
};

#endif // WITH_EDITOR

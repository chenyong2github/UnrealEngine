// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FSkeletalMeshDefaultLODStreamingSettings
{
public:
	ENGINE_API FSkeletalMeshDefaultLODStreamingSettings();

	/**
	 * Initializes default LOD streaming settings by reading them from the passed in config file section.
	 * @param IniFile Preloaded ini file object to load from
	 */
	ENGINE_API void Initialize(const class FConfigFile& IniFile);

	bool IsLODStreamingEnabled() const
	{
		return bSupportLODStreaming;
	}

	int32 GetMaxNumStreamedLODs() const
	{
		return MaxNumStreamedLODs;
	}

	int32 GetMaxNumOptionalLODs() const
	{
		return MaxNumOptionalLODs;
	}

private:
	bool bSupportLODStreaming;
	int32 MaxNumStreamedLODs;
	int32 MaxNumOptionalLODs;
};

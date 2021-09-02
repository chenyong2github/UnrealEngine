// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class FDerivedDataInformation
{
public:

	static double	GetCacheActivityTimeSeconds(bool bGet, bool bLocal);
	static double	GetCacheActivitySizeBytes(bool bGet, bool bLocal);
	static bool		GetHasLocalCache();
	static bool		GetHasRemoteCache();
};

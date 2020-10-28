// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDataLayer;

struct ENGINE_API FDataLayersHelper
{
#if WITH_EDITOR
	static const uint32 NoDataLayerID = 0;

	static uint32 ComputeDataLayerID(const TArray<const UDataLayer*>& DataLayers);
#endif
};
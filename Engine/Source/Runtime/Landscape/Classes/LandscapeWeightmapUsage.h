// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "LandscapeWeightmapUsage.generated.h"

class ULandscapeComponent;

UCLASS(MinimalAPI, NotBlueprintable)
class ULandscapeWeightmapUsage : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY()
	ULandscapeComponent* ChannelUsage[4];

	UPROPERTY()
	FGuid ProceduralLayerGuid;

	int32 FreeChannelCount() const
	{
		int32 Count = 0;

		for (int8 i = 0; i < 4; ++i)
		{
			Count += (ChannelUsage[i] == nullptr) ? 1 : 0;
		}

		return	Count;
	}

	void ClearUsage()
	{
		for (int8 i = 0; i < 4; ++i)
		{
			ChannelUsage[i] = nullptr;
		}
	}
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntity.h"
#include "DMXEntityFader.generated.h"

UCLASS(meta = (DisplayName = "DMX Fader"))
class DMXRUNTIME_API UDMXEntityFader
	: public UDMXEntityUniverseManaged
{
	GENERATED_BODY()

public:
	UPROPERTY()
	bool bIsActive;

};

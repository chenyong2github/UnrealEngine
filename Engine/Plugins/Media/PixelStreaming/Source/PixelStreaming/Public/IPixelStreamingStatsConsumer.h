// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlayerId.h"

// Interface for consuming stats from Pixel Streaming, simply implement and do as you wish with the stats.
// Stats will be pumped to consumers no faster than game thread tick rate, but commonly, 1s for many stats.
class PIXELSTREAMING_API IPixelStreamingStatsConsumer
{
public:
	IPixelStreamingStatsConsumer() {}
	virtual ~IPixelStreamingStatsConsumer() {}
	virtual void ConsumeStat(FPlayerId PlayerId, FName StatName, float StatValue) = 0;
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AWaterBody;
class FGerstnerWaterWaveViewExtension;

class FWaterBodyManager
{
public:
	void Initialize(UWorld* World);
	void Deinitialize();

	/** Called at the beginning of the frame */
	void Update();

	/** 
	 * Register any water body upon addition to the world
	 * @param InWaterBody 
	 * @return int32 the unique sequential index assigned to this water body
	 */
	int32 AddWaterBody(const AWaterBody* InWaterBody);

	/** Unregister any water body upon removal to the world */
	void RemoveWaterBody(const AWaterBody* InWaterBody);

	/** Recomputes wave-related data whenever it changes on one of water bodies. */
	void RequestWaveDataRebuild();

	/** Returns the maximum of all MaxWaveHeight : */
	float GetGlobalMaxWaveHeight() const { return GlobalMaxWaveHeight; }

private:
	TArray<const AWaterBody*> WaterBodies;
	TArray<int32> UnusedWaterBodyIndices;

	float GlobalMaxWaveHeight = 0.0f;

	TSharedPtr<FGerstnerWaterWaveViewExtension, ESPMode::ThreadSafe> GerstnerWaterWaveViewExtension;
};
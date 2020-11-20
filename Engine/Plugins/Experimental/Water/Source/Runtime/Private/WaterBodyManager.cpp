// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyManager.h"
#include "WaterBodyActor.h"
#include "GerstnerWaterWaveViewExtension.h"

void FWaterBodyManager::Initialize(UWorld* World)
{
	if (World != nullptr)
	{
		GerstnerWaterWaveViewExtension = FSceneViewExtensions::NewExtension<FGerstnerWaterWaveViewExtension>(World);
	}
}

void FWaterBodyManager::Deinitialize()
{
	GerstnerWaterWaveViewExtension.Reset();
}

void FWaterBodyManager::Update()
{
	if (GerstnerWaterWaveViewExtension)
	{
		GerstnerWaterWaveViewExtension->WaterBodies = &WaterBodies;
	}
}

int32 FWaterBodyManager::AddWaterBody(const AWaterBody* InWaterBody)
{
	int32 Index = INDEX_NONE;
	if (UnusedWaterBodyIndices.Num())
	{
		Index = UnusedWaterBodyIndices.Pop();
		check(WaterBodies[Index] == nullptr);
		WaterBodies[Index] = InWaterBody;
	}
	else
	{
		Index = WaterBodies.Add(InWaterBody);
	}

	RequestWaveDataRebuild();

	check(Index != INDEX_NONE);
	return Index;
}

void FWaterBodyManager::RemoveWaterBody(const AWaterBody* InWaterBody)
{
	check(InWaterBody->WaterBodyIndex != INDEX_NONE);
	UnusedWaterBodyIndices.Add(InWaterBody->WaterBodyIndex);
	WaterBodies[InWaterBody->WaterBodyIndex] = nullptr;

	RequestWaveDataRebuild();

	// Reset all arrays once there are no more waterbodies
	if (UnusedWaterBodyIndices.Num() == WaterBodies.Num())
	{
		UnusedWaterBodyIndices.Empty();
		WaterBodies.Empty();
	}
}

void FWaterBodyManager::RequestWaveDataRebuild()
{
	if (GerstnerWaterWaveViewExtension)
	{
		GerstnerWaterWaveViewExtension->bRebuildGPUData = true;
	}

	// Recompute the maximum of all MaxWaveHeight : 
	GlobalMaxWaveHeight = 0.0f;
	for (const AWaterBody* WaterBody : WaterBodies)
	{
		if (WaterBody != nullptr)
		{
			GlobalMaxWaveHeight = FMath::Max(GlobalMaxWaveHeight, WaterBody->GetMaxWaveHeight());
		}
	}
}
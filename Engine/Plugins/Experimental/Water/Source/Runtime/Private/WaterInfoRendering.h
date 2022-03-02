// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AWaterZone;
class UWaterBodyComponent;
class FSceneInterface;
class UTextureRenderTarget2D;
class AActor;

namespace WaterInfo
{
	struct FRenderingContext
	{
		AWaterZone* ZoneToRender = nullptr;
		TArray<UWaterBodyComponent*> WaterBodies;
		TArray<AActor*> GroundActors;
		float CaptureZ;
	};
	
void UpdateWaterInfoRendering(
	FSceneInterface* Scene,
	UTextureRenderTarget2D* TextureRenderTarget,
	const FRenderingContext& Context);
}

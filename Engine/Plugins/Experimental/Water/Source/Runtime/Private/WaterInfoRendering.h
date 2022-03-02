// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class AWaterZone;
class UWaterBodyComponent;

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

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"

class ULocalHeightFogComponent;

/** Represents a UVolumetricCloudComponent to the rendering thread, created game side from the component. */
class ENGINE_API FLocalHeightFogSceneProxy
{
public:

	// Initialization constructor.
	FLocalHeightFogSceneProxy(const ULocalHeightFogComponent* InComponent);
	~FLocalHeightFogSceneProxy();

	FTransform FogTransform;

	float FogDensity;
	float FogHeightFalloff;
	float FogHeightOffset;
	float FogRadialAttenuation;

	uint8 FogMode;

	float FogPhaseG;
	FLinearColor FogAlbedo;
	FLinearColor FogEmissive;
private:
};

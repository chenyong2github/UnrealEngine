// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FVolumetricCloudSceneProxy implementation.
=============================================================================*/

#include "LocalHeightFogSceneProxy.h"
#include "Components/LocalHeightFogComponent.h"



FLocalHeightFogSceneProxy::FLocalHeightFogSceneProxy(const ULocalHeightFogComponent* InComponent)
	: FogTransform(InComponent->GetComponentTransform())
	, FogDensity(InComponent->FogDensity)
	, FogHeightFalloff(InComponent->FogHeightFalloff)
	, FogHeightOffset(InComponent->FogHeightOffset)
	, FogRadialAttenuation(InComponent->FogRadialAttenuation)
	, FogMode((uint8)InComponent->FogMode)
	, FogPhaseG(InComponent->FogPhaseG)
	, FogAlbedo(InComponent->FogAlbedo)
	, FogEmissive(InComponent->FogEmissive)
{
}

FLocalHeightFogSceneProxy::~FLocalHeightFogSceneProxy()
{
}



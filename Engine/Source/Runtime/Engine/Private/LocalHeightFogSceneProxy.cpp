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
	, FogHeightOffset(InComponent->FogHeightOffset + InComponent->GetComponentTransform().GetLocation().Z)
	, FogRadialAttenuation(InComponent->FogRadialAttenuation)
	, FogRadialAttenuationSoftness(InComponent->FogRadialAttenuationSoftness)
	, FogPhaseG(InComponent->FogPhaseG)
	, FogAlbedo(InComponent->FogAlbedo)
	, FogEmissive(InComponent->FogEmissive)
{
}

FLocalHeightFogSceneProxy::~FLocalHeightFogSceneProxy()
{
}



// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FVolumetricCloudSceneProxy implementation.
=============================================================================*/

#include "VolumetricCloudProxy.h"
#include "Components/VolumetricCloudComponent.h"



FVolumetricCloudSceneProxy::FVolumetricCloudSceneProxy(const UVolumetricCloudComponent* InComponent)
	: LayerBottomAltitudeKm(InComponent->LayerBottomAltitude)
	, LayerHeightKm(InComponent->LayerHeight)
	, PlanetRadiusKm(InComponent->PlanetRadius)
	, GroundAlbedo(InComponent->GroundAlbedo)
	, AtmosphericLightsContributionFactor(InComponent->AtmosphericLightsContributionFactor)
	, CloudVolumeMaterial(InComponent->Material)
{
}

FVolumetricCloudSceneProxy::~FVolumetricCloudSceneProxy()
{
}



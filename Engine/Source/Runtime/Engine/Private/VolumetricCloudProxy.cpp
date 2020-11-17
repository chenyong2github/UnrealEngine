// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FVolumetricCloudSceneProxy implementation.
=============================================================================*/

#include "VolumetricCloudProxy.h"
#include "Components/VolumetricCloudComponent.h"



FVolumetricCloudSceneProxy::FVolumetricCloudSceneProxy(const UVolumetricCloudComponent* InComponent)
	: LayerBottomAltitudeKm(InComponent->LayerBottomAltitude)
	, LayerHeightKm(InComponent->LayerHeight)
	, TracingStartMaxDistance(InComponent->TracingStartMaxDistance)
	, TracingMaxDistance(InComponent->TracingMaxDistance)
	, PlanetRadiusKm(InComponent->PlanetRadius)
	, GroundAlbedo(InComponent->GroundAlbedo)
	, bUsePerSampleAtmosphericLightTransmittance(InComponent->bUsePerSampleAtmosphericLightTransmittance)
	, SkyLightCloudBottomOcclusion(InComponent->SkyLightCloudBottomOcclusion)
	, ViewSampleCountScale(InComponent->ViewSampleCountScale)
	, ReflectionSampleCountScale(InComponent->ReflectionSampleCountScale)
	, ShadowViewSampleCountScale(InComponent->ShadowViewSampleCountScale)
	, ShadowReflectionSampleCountScale(InComponent->ShadowReflectionSampleCountScale)
	, ShadowTracingDistance(InComponent->ShadowTracingDistance)
	, StopTracingTransmittanceThreshold(InComponent->StopTracingTransmittanceThreshold)
	, CloudVolumeMaterial(InComponent->Material)
{
}

FVolumetricCloudSceneProxy::~FVolumetricCloudSceneProxy()
{
}



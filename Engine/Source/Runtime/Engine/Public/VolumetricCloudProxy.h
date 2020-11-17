// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumetricCloudSceneProxy.h: FVolumetricCloudSceneProxy definition.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"



class UVolumetricCloudComponent;
class FVolumetricCloudRenderSceneInfo;
class UMaterialInterface;



/** Represents a UVolumetricCloudComponent to the rendering thread, created game side from the component. */
class ENGINE_API FVolumetricCloudSceneProxy
{
public:

	// Initialization constructor.
	FVolumetricCloudSceneProxy(const UVolumetricCloudComponent* InComponent);
	~FVolumetricCloudSceneProxy();

	UMaterialInterface* GetCloudVolumeMaterial() const { return CloudVolumeMaterial; }

	FVolumetricCloudRenderSceneInfo* RenderSceneInfo;

	float LayerBottomAltitudeKm;
	float LayerHeightKm;

	float TracingStartMaxDistance;
	float TracingMaxDistance;

	float PlanetRadiusKm;
	FColor GroundAlbedo;
	bool bUsePerSampleAtmosphericLightTransmittance;
	float SkyLightCloudBottomOcclusion;
	
	float ViewSampleCountScale;
	float ReflectionSampleCountScale;
	float ShadowViewSampleCountScale;
	float ShadowReflectionSampleCountScale;
	float ShadowTracingDistance;
	float StopTracingTransmittanceThreshold;
private:

	UMaterialInterface* CloudVolumeMaterial = nullptr;
};



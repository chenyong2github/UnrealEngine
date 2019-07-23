// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyAtmosphereRendering.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderResource.h"
#include "Rendering/SkyAtmosphereCommonData.h"


class FLightSceneInfo;
class USkyAtmosphereComponent;



// Use as a global shader parameter struct and also the CPU structure representing the atmosphere it self.
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FAtmosphereUniformShaderParameters, )
	SHADER_PARAMETER(float, MultiScatteringFactor)
	SHADER_PARAMETER(float, BottomRadius)
	SHADER_PARAMETER(float, TopRadius)
	SHADER_PARAMETER(float, RayleighDensityExpScale)
	SHADER_PARAMETER(FLinearColor, RayleighScattering)
	SHADER_PARAMETER(FLinearColor, MieScattering)
	SHADER_PARAMETER(float, MieDensityExpScale)
	SHADER_PARAMETER(FLinearColor, MieExtinction)
	SHADER_PARAMETER(float, MiePhaseG)
	SHADER_PARAMETER(FLinearColor, MieAbsorption)
	SHADER_PARAMETER(float, AbsorptionDensity0LayerWidth)
	SHADER_PARAMETER(float, AbsorptionDensity0ConstantTerm)
	SHADER_PARAMETER(float, AbsorptionDensity0LinearTerm)
	SHADER_PARAMETER(float, AbsorptionDensity1ConstantTerm)
	SHADER_PARAMETER(float, AbsorptionDensity1LinearTerm)
	SHADER_PARAMETER(FLinearColor, AbsorptionExtinction)
	SHADER_PARAMETER(FLinearColor, GroundAlbedo)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

// Extra data shared by the Sky/Atmosphere system with Base passes.
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSkyAtmosphereBasePassSharedUniformShaderParameters,)
	SHADER_PARAMETER_TEXTURE(Texture3D, CameraAerialPerspectiveVolume)
	SHADER_PARAMETER_SAMPLER(SamplerState, CameraAerialPerspectiveVolumeSampler)
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeStartDepth)
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthResolution)
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthResolutionInv)
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthSliceLength)
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthSliceLengthInv)
	SHADER_PARAMETER(float, ApplyCameraAerialPerspectiveVolume)
END_GLOBAL_SHADER_PARAMETER_STRUCT()



class FSkyAtmosphereRenderSceneInfo
{
public:

	/** Initialization constructor. */
	explicit FSkyAtmosphereRenderSceneInfo(const USkyAtmosphereComponent* InComponent);
	~FSkyAtmosphereRenderSceneInfo();

	/** Prepare the sun light data as a function of current atmosphere state. */
	void PrepareSunLightProxy(FLightSceneInfo& SunLight) const;

	bool IsMultiScatteringEnabled() const { return AtmosphereSetup.MultiScatteringFactor > 0.0f; }
	FLinearColor GetSkyLuminanceFactor() const { return SkyLuminanceFactor; }
	float GetAerialPespectiveViewDistanceScale() const { return AerialPespectiveViewDistanceScale; }

	const TUniformBufferRef<FAtmosphereUniformShaderParameters>& GetAtmosphereUniformBuffer() { return AtmosphereUniformBuffer; }
	TRefCountPtr<IPooledRenderTarget>& GetTransmittanceLutTexture() { return TransmittanceLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetMultiScatteredLuminanceLutTexture() { return MultiScatteredLuminanceLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetDistantSkyLightLutTexture() { return DistantSkyLightLutTexture; }
	FTextureRHIRef GetDistantSkyLightLutTextureRHI();

	const FAtmosphereSetup GetAtmosphereSetup() const { return AtmosphereSetup; }
	const FAtmosphereUniformShaderParameters* GetAtmosphereShaderParameters() const { return &AtmosphereUniformShaderParameters; }

	bool bStaticLightingBuilt;

private:

	FAtmosphereSetup AtmosphereSetup;
	FAtmosphereUniformShaderParameters AtmosphereUniformShaderParameters;

	TUniformBufferRef<FAtmosphereUniformShaderParameters> AtmosphereUniformBuffer;
	TRefCountPtr<IPooledRenderTarget> TransmittanceLutTexture;
	TRefCountPtr<IPooledRenderTarget> MultiScatteredLuminanceLutTexture;
	TRefCountPtr<IPooledRenderTarget> DistantSkyLightLutTexture;

	FLinearColor TransmittanceAtZenith;
	FLinearColor SkyLuminanceFactor;
	float AerialPespectiveViewDistanceScale;
};

bool ShouldRenderSkyAtmosphere(const FSkyAtmosphereRenderSceneInfo* SkyAtmosphere, EShaderPlatform ShaderPlatform);

extern void SetupSkyAtmosphereBasePassSharedUniformShaderParameters(const class FViewInfo& View, FSkyAtmosphereBasePassSharedUniformShaderParameters& OutParameters);



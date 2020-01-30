// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyAtmosphereRendering.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "RendererInterface.h"
#include "RenderResource.h"
#include "Rendering/SkyAtmosphereCommonData.h"


class FScene;
class FViewInfo;
class FLightSceneInfo;
class USkyAtmosphereComponent;
class FSkyAtmosphereSceneProxy;

struct FEngineShowFlags;


// Use as a global shader parameter struct and also the CPU structure representing the atmosphere it self.
// This is static for a version of a component. When a component is changed/tweaked, it is recreated.
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

// These parameters are shared on the view global uniform buffer and are dynamically changed with cvars.
struct FSkyAtmosphereViewSharedUniformShaderParameters
{
	float AerialPerspectiveStartDepth;
	float CameraAerialPerspectiveVolumeDepthResolution;
	float CameraAerialPerspectiveVolumeDepthResolutionInv;
	float CameraAerialPerspectiveVolumeDepthSliceLength;
	float CameraAerialPerspectiveVolumeDepthSliceLengthInv;
	float ApplyCameraAerialPerspectiveVolume;
};



class FSkyAtmosphereRenderSceneInfo
{
public:

	/** Initialization constructor. */
	explicit FSkyAtmosphereRenderSceneInfo(FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy);
	~FSkyAtmosphereRenderSceneInfo();

	const TUniformBufferRef<FAtmosphereUniformShaderParameters>& GetAtmosphereUniformBuffer() { return AtmosphereUniformBuffer; }
	TRefCountPtr<IPooledRenderTarget>& GetTransmittanceLutTexture() { return TransmittanceLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetMultiScatteredLuminanceLutTexture() { return MultiScatteredLuminanceLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetDistantSkyLightLutTexture();

	const FAtmosphereUniformShaderParameters* GetAtmosphereShaderParameters() const { return &AtmosphereUniformShaderParameters; }
	const FSkyAtmosphereSceneProxy& GetSkyAtmosphereSceneProxy() const { return SkyAtmosphereSceneProxy; }

private:

	FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy;

	FAtmosphereUniformShaderParameters AtmosphereUniformShaderParameters;

	TUniformBufferRef<FAtmosphereUniformShaderParameters> AtmosphereUniformBuffer;

	TRefCountPtr<IPooledRenderTarget> TransmittanceLutTexture;
	TRefCountPtr<IPooledRenderTarget> MultiScatteredLuminanceLutTexture;
	TRefCountPtr<IPooledRenderTarget> DistantSkyLightLutTexture;
};



bool ShouldRenderSkyAtmosphere(const FScene* Scene, const FEngineShowFlags& EngineShowFlags);

void InitSkyAtmosphereForScene(FRHICommandListImmediate& RHICmdList, FScene* Scene);
void InitSkyAtmosphereForView(FRHICommandListImmediate& RHICmdList, const FScene* Scene, FViewInfo& View);

extern void SetupSkyAtmosphereViewSharedUniformShaderParameters(const class FViewInfo& View, FSkyAtmosphereViewSharedUniformShaderParameters& OutParameters);

// Prepare the sun light data as a function of the atmosphere state. 
void PrepareSunLightProxy(const FSkyAtmosphereRenderSceneInfo& SkyAtmosphere, uint32 AtmosphereLightIndex, FLightSceneInfo& AtmosphereLight);


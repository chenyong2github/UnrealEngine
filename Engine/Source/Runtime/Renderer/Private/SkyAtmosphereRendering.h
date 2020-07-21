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
#include "RenderGraphResources.h"
#include "SceneView.h"


class FScene;
class FViewInfo;
class FLightSceneInfo;
class FVisibleLightInfo;
class USkyAtmosphereComponent;
class FSkyAtmosphereSceneProxy;
class FProjectedShadowInfo;

class FSkyAtmosphereInternalCommonParameters;
class FVolumeShadowingShaderParametersGlobal0;
class FVolumeShadowingShaderParametersGlobal1;

struct FEngineShowFlags;


// Use as a global shader parameter struct and also the CPU structure representing the atmosphere it self.
// This is static for a version of a component. When a component is changed/tweaked, it is recreated.
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FAtmosphereUniformShaderParameters, )
	SHADER_PARAMETER(float, MultiScatteringFactor)
	SHADER_PARAMETER(float, BottomRadiusKm)
	SHADER_PARAMETER(float, TopRadiusKm)
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
	FVector4 CameraAerialPerspectiveVolumeSizeAndInvSize;
	float AerialPerspectiveStartDepthKm;
	float CameraAerialPerspectiveVolumeDepthResolution;
	float CameraAerialPerspectiveVolumeDepthResolutionInv;
	float CameraAerialPerspectiveVolumeDepthSliceLengthKm;
	float CameraAerialPerspectiveVolumeDepthSliceLengthKmInv;
	float ApplyCameraAerialPerspectiveVolume;
};

// Structure with data necessary to specify a sky render.
struct FSkyAtmosphereRenderContext
{
	///////////////////////////////////
	// Per scene parameters

	bool bUseDepthBoundTestIfPossible;
	bool bForceRayMarching;
	bool bDepthReadDisabled;// Do not apply scene depth texture. As such, only far Z will be considered
	bool bDisableBlending;	// Do not do any blending. The sky will clear the target when rendering a sky reflection capture for instance.
	bool bFastSky;
	bool bFastAerialPerspective;
	bool bFastAerialPerspectiveDepthTest;
	bool bSecondAtmosphereLightEnabled;
	bool bShouldSampleOpaqueShadow;

	FRDGTextureRef TransmittanceLut;
	FRDGTextureRef MultiScatteredLuminanceLut;
	FRDGTextureRef SkyAtmosphereViewLutTexture;
	FRDGTextureRef SkyAtmosphereCameraAerialPerspectiveVolume;

	///////////////////////////////////
	// Per view parameters

	FViewMatrices* ViewMatrices;			// The actual view matrices we use to render the sky
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;

	FRenderTargetBindingSlots RenderTargets;

	FIntRect Viewport;

	bool bLightDiskEnabled;
	bool bRenderSkyPixel;
	float AerialPerspectiveStartDepthInCm;
	float NearClippingDistance;
	ERHIFeatureLevel::Type FeatureLevel;

	TUniformBufferRef<FVolumeShadowingShaderParametersGlobal0> LightShadowShaderParams0UniformBuffer;
	TUniformBufferRef<FVolumeShadowingShaderParametersGlobal1> LightShadowShaderParams1UniformBuffer;

	bool bShouldSampleCloudShadow;
	FRDGTextureRef VolumetricCloudShadowMap[2];

	bool bShouldSampleCloudSkyAO;
	FRDGTextureRef VolumetricCloudSkyAO;

	bool bAPOnCloudMode;
	FRDGTextureRef VolumetricCloudDepthTexture;
	FRDGTextureRef InputCloudLuminanceTransmittanceTexture;

	FSkyAtmosphereRenderContext();

private:
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

	TUniformBufferRef<FSkyAtmosphereInternalCommonParameters>& GetInternalCommonParametersUniformBuffer() { return InternalCommonParametersUniformBuffer; }

private:

	FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy;

	FAtmosphereUniformShaderParameters AtmosphereUniformShaderParameters;

	TUniformBufferRef<FAtmosphereUniformShaderParameters> AtmosphereUniformBuffer;

	TUniformBufferRef<FSkyAtmosphereInternalCommonParameters> InternalCommonParametersUniformBuffer;

	TRefCountPtr<IPooledRenderTarget> TransmittanceLutTexture;
	TRefCountPtr<IPooledRenderTarget> MultiScatteredLuminanceLutTexture;
	TRefCountPtr<IPooledRenderTarget> DistantSkyLightLutTexture;
};



bool ShouldRenderSkyAtmosphere(const FScene* Scene, const FEngineShowFlags& EngineShowFlags);

void InitSkyAtmosphereForScene(FRHICommandListImmediate& RHICmdList, FScene* Scene);
void InitSkyAtmosphereForView(FRHICommandListImmediate& RHICmdList, const FScene* Scene, FViewInfo& View);

extern void SetupSkyAtmosphereViewSharedUniformShaderParameters(const class FViewInfo& View, const FSkyAtmosphereSceneProxy& SkyAtmosphereProxy, FSkyAtmosphereViewSharedUniformShaderParameters& OutParameters);

// Prepare the sun light data as a function of the atmosphere state. 
void PrepareSunLightProxy(const FSkyAtmosphereRenderSceneInfo& SkyAtmosphere, uint32 AtmosphereLightIndex, FLightSceneInfo& AtmosphereLight);


float GetValidAerialPerspectiveStartDepthInCm(const FViewInfo& View, const FSkyAtmosphereSceneProxy& SkyAtmosphereProxy);

struct SkyAtmosphereLightShadowData
{
	const FLightSceneInfo* LightVolumetricShadowSceneinfo0 = nullptr;
	const FLightSceneInfo* LightVolumetricShadowSceneinfo1 = nullptr;
	const FProjectedShadowInfo* ProjectedShadowInfo0 = nullptr;
	const FProjectedShadowInfo* ProjectedShadowInfo1 = nullptr;
};
bool ShouldSkySampleAtmosphereLightsOpaqueShadow(const FScene& Scene, const TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos, SkyAtmosphereLightShadowData& LightShadowData);
void GetSkyAtmosphereLightsUniformBuffers(
	TUniformBufferRef<FVolumeShadowingShaderParametersGlobal0>& OutLightShadowShaderParams0UniformBuffer,
	TUniformBufferRef<FVolumeShadowingShaderParametersGlobal1>& OutLightShadowShaderParams1UniformBuffer,
	const SkyAtmosphereLightShadowData& LightShadowData,
	const FViewInfo& ViewInfo,
	const bool bShouldSampleOpaqueShadow,
	const EUniformBufferUsage UniformBufferUsage);
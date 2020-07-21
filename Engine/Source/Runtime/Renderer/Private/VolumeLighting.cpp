// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LightShadowShaderParameters.h
=============================================================================*/

#include "VolumeLighting.h"
#include "SceneView.h"
#include "SceneRendering.h"
#include "LightSceneInfo.h"
#include "ShadowRendering.h"
#include "Components/LightComponent.h"
#include "Engine/MapBuildDataRegistry.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVolumeShadowingShaderParametersGlobal0, "Light0Shadow");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVolumeShadowingShaderParametersGlobal1, "Light1Shadow");

const FProjectedShadowInfo* GetLastCascadeShadowInfo(const FLightSceneProxy* LightProxy, const FVisibleLightInfo& VisibleLightInfo)
{
	for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo.ShadowsToProject.Num(); ShadowIndex++)
	{
		const FProjectedShadowInfo* ProjectedShadowInfo = VisibleLightInfo.ShadowsToProject[ShadowIndex];

		if (ProjectedShadowInfo->bAllocated
			&& ProjectedShadowInfo->bWholeSceneShadow
			&& !ProjectedShadowInfo->bRayTracedDistanceField)
		{
			return ProjectedShadowInfo;
		}
	}

	return NULL;
}

static auto SetVolumeShadowingDefaultShaderParametersGlobal = [](auto& ShaderParams)
{
	ShaderParams.WorldToShadowMatrix = FMatrix::Identity;
	ShaderParams.ShadowmapMinMax = FVector4(1.0f);
	ShaderParams.DepthBiasParameters = FVector4(1.0f);
	ShaderParams.ShadowInjectParams = FVector4(1.0f);
	memset(ShaderParams.ClippingPlanes.GetData(), 0, sizeof(ShaderParams.ClippingPlanes));
	ShaderParams.bStaticallyShadowed = 0;
	ShaderParams.WorldToStaticShadowMatrix = FMatrix::Identity;
	ShaderParams.StaticShadowBufferSize = FVector4(1.0f);
	ShaderParams.ShadowDepthTexture = GWhiteTexture->TextureRHI;
	ShaderParams.StaticShadowDepthTexture = GWhiteTexture->TextureRHI;
	ShaderParams.ShadowDepthTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	ShaderParams.StaticShadowDepthTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	memset(ShaderParams.OnePassPointShadowProjection.ShadowViewProjectionMatrices.GetData(), 0, sizeof(ShaderParams.OnePassPointShadowProjection.ShadowViewProjectionMatrices));
	ShaderParams.OnePassPointShadowProjection.InvShadowmapResolution = 1.0f;
	ShaderParams.OnePassPointShadowProjection.ShadowDepthCubeTexture = GBlackTextureDepthCube->TextureRHI.GetReference();
	ShaderParams.OnePassPointShadowProjection.ShadowDepthCubeTexture2 = GBlackTextureDepthCube->TextureRHI.GetReference();
	ShaderParams.OnePassPointShadowProjection.ShadowDepthCubeTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 0, 0, SCF_Less>::GetRHI();
};



static auto GetVolumeShadowingShaderParametersGlobal = [](
	auto& ShaderParams,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	const FProjectedShadowInfo* ShadowInfo,
	int32 InnerSplitIndex)
{
	const bool bDynamicallyShadowed = ShadowInfo != NULL;
	if (bDynamicallyShadowed)
	{
		FVector4 ShadowmapMinMaxValue;
		ShaderParams.WorldToShadowMatrix = ShadowInfo->GetWorldToShadowMatrix(ShaderParams.ShadowmapMinMax);
	}

	// default to ignore the plane
	FVector4 Planes[2] = { FVector4(0, 0, 0, -1), FVector4(0, 0, 0, -1) };
	// .zw:DistanceFadeMAD to use MAD for efficiency in the shader, default to ignore the plane
	FVector4 ShadowInjectParamValue(1, 1, 0, 0);

	if (InnerSplitIndex != INDEX_NONE)
	{
		FShadowCascadeSettings ShadowCascadeSettings;

		LightSceneInfo->Proxy->GetShadowSplitBounds(View, InnerSplitIndex, LightSceneInfo->IsPrecomputedLightingValid(), &ShadowCascadeSettings);
		ensureMsgf(ShadowCascadeSettings.ShadowSplitIndex != INDEX_NONE, TEXT("FLightSceneProxy::GetShadowSplitBounds did not return an initialized ShadowCascadeSettings"));

		// near cascade plane
		{
			ShadowInjectParamValue.X = ShadowCascadeSettings.SplitNearFadeRegion == 0 ? 1.0f : 1.0f / ShadowCascadeSettings.SplitNearFadeRegion;
			Planes[0] = FVector4((FVector)(ShadowCascadeSettings.NearFrustumPlane), -ShadowCascadeSettings.NearFrustumPlane.W);
		}

		uint32 CascadeCount = LightSceneInfo->Proxy->GetNumViewDependentWholeSceneShadows(View, LightSceneInfo->IsPrecomputedLightingValid());

		// far cascade plane
		if (InnerSplitIndex != CascadeCount - 1)
		{
			ShadowInjectParamValue.Y = 1.0f / (ShadowCascadeSettings.SplitFarFadeRegion == 0.0f ? 0.0001f : ShadowCascadeSettings.SplitFarFadeRegion);
			Planes[1] = FVector4((FVector)(ShadowCascadeSettings.FarFrustumPlane), -ShadowCascadeSettings.FarFrustumPlane.W);
		}

		const FVector2D FadeParams = LightSceneInfo->Proxy->GetDirectionalLightDistanceFadeParameters(View.GetFeatureLevel(), LightSceneInfo->IsPrecomputedLightingValid(), View.MaxShadowCascades);

		// setup constants for the MAD in shader
		ShadowInjectParamValue.Z = FadeParams.Y;
		ShadowInjectParamValue.W = -FadeParams.X * FadeParams.Y;
	}
	ShaderParams.ShadowInjectParams = ShadowInjectParamValue;
	ShaderParams.ClippingPlanes[0] = Planes[0];
	ShaderParams.ClippingPlanes[1] = Planes[1];

	ELightComponentType LightType = (ELightComponentType)LightSceneInfo->Proxy->GetLightType();
	FRHITexture* ShadowDepthTextureResource = nullptr;
	if (bDynamicallyShadowed)
	{
		ShaderParams.DepthBiasParameters = FVector4(ShadowInfo->GetShaderDepthBias(), ShadowInfo->GetShaderSlopeDepthBias(), ShadowInfo->GetShaderMaxSlopeDepthBias(), 1.0f / (ShadowInfo->MaxSubjectZ - ShadowInfo->MinSubjectZ));

		if (LightType == LightType_Point || LightType == LightType_Rect)
		{
			ShadowDepthTextureResource = GBlackTexture->TextureRHI->GetTexture2D();
		}
		else
		{
			ShadowDepthTextureResource = ShadowInfo->RenderTargets.DepthTarget->GetRenderTargetItem().ShaderResourceTexture.GetReference();
		}
	}
	else
	{
		ShadowDepthTextureResource = GBlackTexture->TextureRHI->GetTexture2D();
	}
	check(ShadowDepthTextureResource)
		ShaderParams.ShadowDepthTexture = ShadowDepthTextureResource;
	ShaderParams.ShadowDepthTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	const FStaticShadowDepthMap* StaticShadowDepthMap = LightSceneInfo->Proxy->GetStaticShadowDepthMap();
	const uint32 bStaticallyShadowedValue = LightSceneInfo->IsPrecomputedLightingValid() && StaticShadowDepthMap && StaticShadowDepthMap->Data && StaticShadowDepthMap->TextureRHI ? 1 : 0;
	FRHITexture* StaticShadowDepthMapTexture = bStaticallyShadowedValue ? StaticShadowDepthMap->TextureRHI : GWhiteTexture->TextureRHI;
	const FMatrix WorldToStaticShadow = bStaticallyShadowedValue ? StaticShadowDepthMap->Data->WorldToLight : FMatrix::Identity;
	const FVector4 StaticShadowBufferSizeValue = bStaticallyShadowedValue ? FVector4(StaticShadowDepthMap->Data->ShadowMapSizeX, StaticShadowDepthMap->Data->ShadowMapSizeY, 1.0f / StaticShadowDepthMap->Data->ShadowMapSizeX, 1.0f / StaticShadowDepthMap->Data->ShadowMapSizeY) : FVector4(0, 0, 0, 0);

	ShaderParams.bStaticallyShadowed = bStaticallyShadowedValue;

	ShaderParams.StaticShadowDepthTexture = StaticShadowDepthMapTexture;
	ShaderParams.StaticShadowDepthTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	ShaderParams.WorldToStaticShadowMatrix = WorldToStaticShadow;
	ShaderParams.StaticShadowBufferSize = StaticShadowBufferSizeValue;

	//
	// See FOnePassPointShadowProjectionShaderParameters from ShadowRendering.h
	//
	FRHITexture* ShadowDepthTextureValue = ShadowInfo
		? ShadowInfo->RenderTargets.DepthTarget->GetRenderTargetItem().ShaderResourceTexture->GetTextureCube()
		: GBlackTextureDepthCube->TextureRHI.GetReference();
	if (!ShadowDepthTextureValue)
	{
		ShadowDepthTextureValue = GBlackTextureDepthCube->TextureRHI.GetReference();
	}
	ShaderParams.OnePassPointShadowProjection.ShadowDepthCubeTexture = ShadowDepthTextureValue;
	ShaderParams.OnePassPointShadowProjection.ShadowDepthCubeTexture2 = ShadowDepthTextureValue;
	ShaderParams.OnePassPointShadowProjection.ShadowDepthCubeTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 0, 0, SCF_Less>::GetRHI();

	if (bDynamicallyShadowed)
	{
		if (ShadowInfo)
		{
			memcpy(ShaderParams.OnePassPointShadowProjection.ShadowViewProjectionMatrices.GetData(), ShadowInfo->OnePassShadowViewProjectionMatrices.GetData(), ShadowInfo->OnePassShadowViewProjectionMatrices.Num() * sizeof(FMatrix));
			ShaderParams.OnePassPointShadowProjection.InvShadowmapResolution = 1.0f / float(ShadowInfo->ResolutionX);
		}
		else
		{
			memset(ShaderParams.OnePassPointShadowProjection.ShadowViewProjectionMatrices.GetData(), 0, sizeof(ShaderParams.OnePassPointShadowProjection.ShadowViewProjectionMatrices));
			ShaderParams.OnePassPointShadowProjection.InvShadowmapResolution = 0.0f;
		}
	}
};



void SetVolumeShadowingShaderParameters(
	FVolumeShadowingShaderParametersGlobal0& ShaderParams,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	const FProjectedShadowInfo* ShadowInfo,
	int32 InnerSplitIndex)
{
	FLightShaderParameters LightParameters;
	LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);
	ShaderParams.Position = LightParameters.Position;
	ShaderParams.InvRadius = LightParameters.InvRadius;

	GetVolumeShadowingShaderParametersGlobal(
		ShaderParams.VolumeShadowingShaderParameters,
		View,
		LightSceneInfo,
		ShadowInfo,
		InnerSplitIndex);
}

void SetVolumeShadowingShaderParameters(
	FVolumeShadowingShaderParametersGlobal1& ShaderParams,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	const FProjectedShadowInfo* ShadowInfo,
	int32 InnerSplitIndex)
{
	FLightShaderParameters LightParameters;
	LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);
	ShaderParams.Position = LightParameters.Position;
	ShaderParams.InvRadius = LightParameters.InvRadius;

	GetVolumeShadowingShaderParametersGlobal(
		ShaderParams.VolumeShadowingShaderParameters,
		View,
		LightSceneInfo,
		ShadowInfo,
		InnerSplitIndex);
}

void SetVolumeShadowingDefaultShaderParameters(FVolumeShadowingShaderParametersGlobal0& ShaderParams)
{
	ShaderParams.Position = FVector(1.0f);
	ShaderParams.InvRadius = 1.0f;
	SetVolumeShadowingDefaultShaderParametersGlobal(ShaderParams.VolumeShadowingShaderParameters);
}

void SetVolumeShadowingDefaultShaderParameters(FVolumeShadowingShaderParametersGlobal1& ShaderParams)
{
	ShaderParams.Position = FVector(1.0f);
	ShaderParams.InvRadius = 1.0f;
	SetVolumeShadowingDefaultShaderParametersGlobal(ShaderParams.VolumeShadowingShaderParameters);
}


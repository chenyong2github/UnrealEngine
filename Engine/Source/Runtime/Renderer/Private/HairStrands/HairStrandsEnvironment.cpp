// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "HairStrandsEnvironment.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "SceneTextureParameters.h"
#include "SceneRenderTargetParameters.h"
#include "RenderGraph.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "PixelShaderUtils.h"
#include "SystemTextures.h"
#include "HairStrandsRendering.h"
#include "ReflectionEnvironment.h"
#include "ScenePrivate.h"
#include "RenderGraphEvent.h"

///////////////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairSkylightingEnable = 1;
static FAutoConsoleVariableRef CVarHairSkylightingEnable(TEXT("r.HairStrands.SkyLightingEnable"), GHairSkylightingEnable, TEXT("Enable sky lighting on hair."));

static int32 GHairSkyAOEnable = 1;
static FAutoConsoleVariableRef CVarHairSkyAOEnable(TEXT("r.HairStrands.SkyAOEnable"), GHairSkyAOEnable, TEXT("Enable (sky) AO on hair."));

static float GHairSkylightingConeAngle = 3;
static FAutoConsoleVariableRef CVarHairSkylightingConeAngle(TEXT("r.HairStrands.SkyLightingConeAngle"), GHairSkylightingConeAngle, TEXT("Cone angle for tracing sky lighting on hair."));

static bool GetHairStrandsSkyLightingEnable() { return GHairSkylightingEnable > 0; }
static bool GetHairStrandsSkyAOEnable() { return GHairSkyAOEnable > 0; }
static float GetHairStrandsSkyLightingConeAngle() { return FMath::Max(0.f, GHairSkylightingConeAngle); }

///////////////////////////////////////////////////////////////////////////////////////////////////

DECLARE_GPU_STAT_NAMED(HairStrandsReflectionEnvironment, TEXT("Hair Strands Reflection Environment"));

class FHairEnvironmentLightingPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairEnvironmentLightingPS);
	SHADER_USE_PARAMETER_STRUCT(FHairEnvironmentLightingPS, FGlobalShader)


		class FRenderMode : SHADER_PERMUTATION_INT("PERMUTATION_RENDER_MODE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FRenderMode>;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(FVector, Voxel_MinAABB)
		SHADER_PARAMETER(FVector, Voxel_MaxAABB)
		SHADER_PARAMETER(uint32, Voxel_Resolution)
		SHADER_PARAMETER(float, Voxel_DensityScale)
		SHADER_PARAMETER(float, Voxel_DepthBiasScale)
		SHADER_PARAMETER(float, Voxel_TanConeAngle)

		SHADER_PARAMETER(float, AO_Power)
		SHADER_PARAMETER(float, AO_Intensity)

		SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)

		SHADER_PARAMETER_TEXTURE(Texture2D, HairCategorizationTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, HairVisibilityNodeOffsetAndCount)
		SHADER_PARAMETER_SRV(Buffer, HairVisibilityNodeData)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)

		SHADER_PARAMETER_RDG_TEXTURE(TEXTURE3D, HairEnergyLUTTexture)
		SHADER_PARAMETER_RDG_TEXTURE(TEXTURE3D, HairScatteringLUTTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HairLUTSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionsParameters)
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCaptureData)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)

		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()
}; // FHairEnvironmentLightingPS

IMPLEMENT_GLOBAL_SHADER(FHairEnvironmentLightingPS, "/Engine/Private/HairStrands/HairStrandsEnvironmentLighting.usf", "MainPS", SF_Pixel);

enum class EEnvRenderMode : uint8
{
	Lighting,
	AO
};

static void AddHairStrandsEnvironmentPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	EEnvRenderMode RenderMode,
	const FHairStrandsVisibilityData* HairVisibilityData,
	const FHairStrandsClusterData* ClusterData,
	FRDGTextureRef Output0,
	FRDGTextureRef Output1)
{
	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	// Render the reflection environment with tiled deferred culling
	const bool bHasBoxCaptures = (View.NumBoxReflectionCaptures > 0);
	const bool bHasSphereCaptures = (View.NumSphereReflectionCaptures > 0);
	FHairEnvironmentLightingPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairEnvironmentLightingPS::FParameters>();

	const FHairLUT InHairLUT = GetHairLUT(GraphBuilder.RHICmdList, View);
	PassParameters->HairEnergyLUTTexture = GraphBuilder.RegisterExternalTexture(InHairLUT.Textures[HairLUTType_MeanEnergy], TEXT("HairMeanEnergyLUTTexture"));;
	PassParameters->HairScatteringLUTTexture = GraphBuilder.RegisterExternalTexture(InHairLUT.Textures[HairLUTType_DualScattering], TEXT("HairScatteringEnergyLUTTexture"));
	PassParameters->HairLUTSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	if (ClusterData)
	{
		PassParameters->Voxel_MinAABB = ClusterData->GetMinBound();
		PassParameters->Voxel_MaxAABB = ClusterData->GetMaxBound();
		PassParameters->Voxel_Resolution = ClusterData->GetResolution();
		PassParameters->Voxel_DensityTexture = GraphBuilder.RegisterExternalTexture(ClusterData->VoxelResources.DensityTexture);
		PassParameters->Voxel_DensityScale = GetHairStrandsVoxelizationDensityScale();
		PassParameters->Voxel_DepthBiasScale = GetHairStrandsVoxelizationDepthBiasScale();
		PassParameters->Voxel_TanConeAngle = FMath::Tan(FMath::DegreesToRadians(GetHairStrandsSkyLightingConeAngle()));
	}
	else
	{
		// .. todo dummy 3D texture
		PassParameters->Voxel_DensityTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
	}

	PassParameters->PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
	PassParameters->PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	PassParameters->SceneTextures = SceneTextures;
	SetupSceneTextureSamplers(&PassParameters->SceneTextureSamplers);

	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->ReflectionCaptureData = View.ReflectionCaptureUniformBuffer;
	{
		FReflectionUniformParameters ReflectionUniformParameters;
		SetupReflectionUniformParameters(View, ReflectionUniformParameters);
		PassParameters->ReflectionsParameters = CreateUniformBufferImmediate(ReflectionUniformParameters, UniformBuffer_SingleDraw);
	}
	PassParameters->ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;


	// Bind hair data
	if (HairVisibilityData)
	{
		PassParameters->HairCategorizationTexture = HairVisibilityData->CategorizationTexture->GetRenderTargetItem().ShaderResourceTexture;
		PassParameters->HairVisibilityNodeOffsetAndCount = HairVisibilityData->NodeIndex->GetRenderTargetItem().ShaderResourceTexture;
		PassParameters->HairVisibilityNodeData = HairVisibilityData->NodeDataSRV;
	}

	PassParameters->AO_Power = 0;
	PassParameters->AO_Intensity = 0;
	if (RenderMode == EEnvRenderMode::AO)
	{
		const FFinalPostProcessSettings& Settings = View.FinalPostProcessSettings;
		PassParameters->AO_Power = Settings.AmbientOcclusionPower;
		PassParameters->AO_Intensity = Settings.AmbientOcclusionIntensity;
	}

	check(Output0);
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Output0, ERenderTargetLoadAction::ELoad);
	if (Output1)
	{
		PassParameters->RenderTargets[1] = FRenderTargetBinding(Output1, ERenderTargetLoadAction::ELoad);
	}

	FHairEnvironmentLightingPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairEnvironmentLightingPS::FRenderMode>(RenderMode == EEnvRenderMode::AO ? 1 : 0);
	TShaderMapRef<FHairEnvironmentLightingPS> PixelShader(View.ShaderMap, PermutationVector);
	ClearUnusedGraphResources(*PixelShader, PassParameters);

	GraphBuilder.AddPass(
		(RenderMode == EEnvRenderMode::Lighting) ? 
		RDG_EVENT_NAME("HairStrandsEnvironment %dx%d", View.ViewRect.Width(), View.ViewRect.Height()) : 
		RDG_EVENT_NAME("HairStrandsAO %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, &View, PixelShader, RenderMode](FRHICommandList& InRHICmdList)
	{
		InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		FPixelShaderUtils::InitFullscreenPipelineState(InRHICmdList, View.ShaderMap, *PixelShader, GraphicsPSOInit);

		if (RenderMode == EEnvRenderMode::AO)
		{
			GraphicsPSOInit.BlendState = TStaticBlendState<
			CW_RGBA, BO_Min, BF_SourceColor, BF_DestColor, BO_Add, BF_Zero, BF_DestAlpha
			>::GetRHI();
		}
		else //if (RenderMode == EEnvRenderMode::Lighting)
		{
			GraphicsPSOInit.BlendState = TStaticBlendState<
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Max, BF_SourceAlpha, BF_DestAlpha
			>::GetRHI();
		}

		SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);
		SetShaderParameters(InRHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);
		FPixelShaderUtils::DrawFullscreenTriangle(InRHICmdList);
	});
}

static const FHairStrandsClusterData* GetClusterData(const FHairStrandsDatas* HairDatas, uint32 ViewIndex, uint32 ClusterIndex)
{
	return
		(
			HairDatas && 
			ViewIndex < uint32(HairDatas->HairClusterPerViews.Views.Num()) &&
			ClusterIndex < uint32(HairDatas->HairClusterPerViews.Views[ViewIndex].Datas.Num())
		) ?
		&HairDatas->HairClusterPerViews.Views[ViewIndex].Datas[ClusterIndex] :
		nullptr;
}

void RenderHairStrandsEnvironmentLighting(
	FRDGBuilder& GraphBuilder,
	const uint32 ViewIndex,
	const TArray<FViewInfo>& Views, 
	const FHairStrandsDatas* HairDatas,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneColorSubPixelTexture)
{
	if (!GetHairStrandsSkyLightingEnable())
		return;

	check(ViewIndex < uint32(Views.Num()));
	const FViewInfo& View = Views[ViewIndex];
	const FHairStrandsVisibilityData* HairVisibilityData = HairDatas ? &HairDatas->HairVisibilityViews.HairDatas[ViewIndex] : nullptr;
	const bool bRenderHairLighting = HairVisibilityData && HairVisibilityData->NodeIndex && HairVisibilityData->NodeDataSRV;
	if (!bRenderHairLighting)
	{
		return;
	}

	// @hair_todo: add support for multiple clusters on screen
	const FHairStrandsClusterData* ClusterData = GetClusterData(HairDatas, ViewIndex, 0);

	// @hair_todo: 
	// * Add support for : BentNormal, global AO, SSR, DFSO
	// * Add local reflection probe, current take into account only the sky lighting
	//RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandEnvironment);
	AddHairStrandsEnvironmentPass(GraphBuilder, View, EEnvRenderMode::Lighting, HairVisibilityData, ClusterData, SceneColorTexture, SceneColorSubPixelTexture);
}

void RenderHairStrandsAmbientOcclusion(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	const FHairStrandsDatas* HairDatas,
	const TRefCountPtr<IPooledRenderTarget>& InAOTexture)
{
	if (!GetHairStrandsSkyAOEnable() || Views.Num() == 0 || !InAOTexture)
		return;

	for (uint32 ViewIndex = 0; ViewIndex < uint32(Views.Num()); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];
		const FHairStrandsVisibilityData* HairVisibilityData = HairDatas ? &HairDatas->HairVisibilityViews.HairDatas[ViewIndex] : nullptr;
		const bool bRenderHairLighting = HairVisibilityData && HairVisibilityData->NodeIndex && HairVisibilityData->NodeDataSRV;
		if (!bRenderHairLighting)
		{
			return;
		}


		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef AOTexture = InAOTexture ? GraphBuilder.RegisterExternalTexture(InAOTexture, TEXT("AOTexture")) : nullptr;

		// @hair_todo: add support for multiple clusters on screen
		const FHairStrandsClusterData* ClusterData = GetClusterData(HairDatas, ViewIndex, 0);

		AddHairStrandsEnvironmentPass(GraphBuilder, View, EEnvRenderMode::AO, HairVisibilityData, ClusterData, AOTexture, nullptr);

		GraphBuilder.Execute();
	}
}

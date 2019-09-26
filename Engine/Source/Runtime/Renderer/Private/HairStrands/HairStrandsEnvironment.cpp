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

///////////////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairSkylightingEnable = 1;
static FAutoConsoleVariableRef CVarHairSkylightingEnable(TEXT("r.HairStrands.SkyLightingEnable"), GHairSkylightingEnable, TEXT("Enable sky lighting on hair."));

static int32 GHairSkyAOEnable = 0;
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

void RenderHairStrandsEnvironmentLighting(
	FRDGBuilder& GraphBuilder,
	const uint32 ViewIndex,
	const TArray<FViewInfo>& Views, 
	const FHairStrandsDatas* HairDatas,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneColorSubPixelTexture)
{
	const FViewInfo& View = Views[ViewIndex];
	const FHairStrandsVisibilityData* HairVisibilityData = HairDatas ? &HairDatas->HairVisibilityViews.HairDatas[ViewIndex] : nullptr;
	const bool bRenderHairLighting = HairVisibilityData && HairVisibilityData->NodeIndex && HairVisibilityData->NodeDataSRV && (GetHairStrandsSkyLightingEnable() || GetHairStrandsSkyAOEnable());
	if (!bRenderHairLighting)
	{
		return;
	}

	// @hair_todo: 
	// * Add support for : BentNormal, global AO, SSR, DFSO
	// * Add local reflection probe, current take into account only the sky lighting

	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsReflectionEnvironment);

	enum class EEnvRenderMode : uint8
	{
		Lighting,
		AO
	};
	const FHairLUT InHairLUT = GetHairLUT(GraphBuilder.RHICmdList, View);

	// Setup the parameters of the shader.
	auto RenderEnv = [&](EEnvRenderMode RenderMode)
	{
		// Render the reflection environment with tiled deferred culling
		const bool bHasBoxCaptures = (View.NumBoxReflectionCaptures > 0);
		const bool bHasSphereCaptures = (View.NumSphereReflectionCaptures > 0);
		FHairEnvironmentLightingPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairEnvironmentLightingPS::FParameters>();
				
		PassParameters->HairEnergyLUTTexture = GraphBuilder.RegisterExternalTexture(InHairLUT.Textures[HairLUTType_MeanEnergy], TEXT("HairMeanEnergyLUTTexture"));;
		PassParameters->HairScatteringLUTTexture = GraphBuilder.RegisterExternalTexture(InHairLUT.Textures[HairLUTType_DualScattering], TEXT("HairScatteringEnergyLUTTexture"));
		PassParameters->HairLUTSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		if (HairDatas && ViewIndex < uint32(HairDatas->HairClusterPerViews.Views.Num()) && HairDatas->HairClusterPerViews.Views[ViewIndex].Datas.Num() > 0)
		{
			const uint32 ClusterIndex = 0; // Support only one cluster at the moment
			const FHairStrandsClusterData& ClusterData = HairDatas->HairClusterPerViews.Views[ViewIndex].Datas[ClusterIndex];

			PassParameters->Voxel_MinAABB = ClusterData.GetMinBound();
			PassParameters->Voxel_MaxAABB = ClusterData.GetMaxBound();
			PassParameters->Voxel_Resolution = ClusterData.GetResolution();
			PassParameters->Voxel_DensityTexture = GraphBuilder.RegisterExternalTexture(ClusterData.VoxelResources.DensityTexture);
			PassParameters->Voxel_DensityScale = GetHairStrandsVoxelizationDensityScale();
			PassParameters->Voxel_DepthBiasScale = GetHairStrandsVoxelizationDepthBiasScale();
			PassParameters->Voxel_TanConeAngle = FMath::Tan(FMath::DegreesToRadians(GetHairStrandsSkyLightingConeAngle()));
		}
		else
		{
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

		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);
		if (RenderMode == EEnvRenderMode::Lighting)
			PassParameters->RenderTargets[1] = FRenderTargetBinding(SceneColorSubPixelTexture, ERenderTargetLoadAction::ELoad);

		FHairEnvironmentLightingPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FHairEnvironmentLightingPS::FRenderMode>(RenderMode == EEnvRenderMode::AO ? 1 : 0);
		TShaderMapRef<FHairEnvironmentLightingPS> PixelShader(View.ShaderMap, PermutationVector);
		ClearUnusedGraphResources(*PixelShader, PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsEnvironment %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, &View, PixelShader, bRenderHairLighting, RenderMode](FRHICommandList& InRHICmdList)
		{
			InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			FPixelShaderUtils::InitFullscreenPipelineState(InRHICmdList, View.ShaderMap, *PixelShader, GraphicsPSOInit);

			if (RenderMode == EEnvRenderMode::AO)
				GraphicsPSOInit.BlendState = TStaticBlendState<
				CW_RGBA, BO_Add, BF_Zero, BF_SourceAlpha, BO_Add, BF_Zero, BF_One
				>::GetRHI();
			else //if (RenderMode == EEnvRenderMode::Lighting)
				GraphicsPSOInit.BlendState = TStaticBlendState<
				CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
				CW_RGBA, BO_Add, BF_One, BF_One, BO_Max, BF_SourceAlpha, BF_DestAlpha
				>::GetRHI();

			SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);
			SetShaderParameters(InRHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);
			FPixelShaderUtils::DrawFullscreenTriangle(InRHICmdList);
		});
	}; // hair

	// @hair_todo: move hair AO actually into the AO pass.
	if (GetHairStrandsSkyAOEnable())
		RenderEnv(EEnvRenderMode::AO);

	if (GetHairStrandsSkyLightingEnable())
		RenderEnv(EEnvRenderMode::Lighting);
}

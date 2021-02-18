// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenScreenSpaceBentNormal.cpp
=============================================================================*/

#include "LumenScreenProbeGather.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"

class FScreenSpaceBentNormalCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceBentNormalCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceBentNormalCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWScreenBentNormal)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, LightingChannelsTexture)
		SHADER_PARAMETER(FVector4, HZBUvFactorAndInvFactor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FurthestHZBTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, FurthestHZBTextureSampler)
	END_SHADER_PARAMETER_STRUCT()

	class FNumPixelRays : SHADER_PERMUTATION_SPARSE_INT("NUM_PIXEL_RAYS", 4, 8, 16);
	using FPermutationDomain = TShaderPermutationDomain<FNumPixelRays>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize() 
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenSpaceBentNormalCS, "/Engine/Private/Lumen/LumenScreenSpaceBentNormal.usf", "ScreenSpaceBentNormalCS", SF_Compute);

FScreenSpaceBentNormalParameters ComputeScreenSpaceBentNormal(
	FRDGBuilder& GraphBuilder, 
	const FScene* Scene,
	const FViewInfo& View, 
	const FMinimalSceneTextures& SceneTextures,
	FRDGTextureRef LightingChannelsTexture,
	const FScreenProbeParameters& ScreenProbeParameters)
{
	FScreenSpaceBentNormalParameters OutParameters;

	FRDGTextureDesc ScreenBentNormalDesc(FRDGTextureDesc::Create2D(GetSceneTextureExtent(), PF_R8G8B8A8, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	FRDGTextureRef ScreenBentNormal = GraphBuilder.CreateTexture(ScreenBentNormalDesc, TEXT("Lumen.ScreenProbeGather.ScreenBentNormal"));

	int32 NumPixelRays = 4;

	if (View.FinalPostProcessSettings.LumenFinalGatherQuality >= 6.0f)
	{
		NumPixelRays = 16;
	}
	else if (View.FinalPostProcessSettings.LumenFinalGatherQuality >= 2.0f)
	{
		NumPixelRays = 8;
	}

	{
		FScreenSpaceBentNormalCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenSpaceBentNormalCS::FParameters>();
		PassParameters->RWScreenBentNormal = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ScreenBentNormal));
		PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->LightingChannelsTexture = LightingChannelsTexture;

		const FVector2D ViewportUVToHZBBufferUV(
			float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
			float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y)
		);

		PassParameters->HZBUvFactorAndInvFactor = FVector4(
			ViewportUVToHZBBufferUV.X,
			ViewportUVToHZBBufferUV.Y,
			1.0f / ViewportUVToHZBBufferUV.X,
			1.0f / ViewportUVToHZBBufferUV.Y);

		PassParameters->FurthestHZBTexture = View.HZB;
		PassParameters->FurthestHZBTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();

		FScreenSpaceBentNormalCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenSpaceBentNormalCS::FNumPixelRays >(NumPixelRays);
		auto ComputeShader = View.ShaderMap->GetShader<FScreenSpaceBentNormalCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ScreenSpaceBentNormal"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), FScreenSpaceBentNormalCS::GetGroupSize()));
	}

	OutParameters.ScreenBentNormal = ScreenBentNormal;
	OutParameters.UseScreenBentNormal = 1;
	return OutParameters;
}
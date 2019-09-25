// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRendering.cpp: Scene rendering.
=============================================================================*/

#include "ScenePrivate.h"
#include "RenderGraph.h"
#include "PixelShaderUtils.h"
#include "SceneTextureParameters.h"
#include "PostProcess/PostProcessing.h" // for FPostProcessVS


static TAutoConsoleVariable<int32> CVarHZBBuildUseCompute(
	TEXT("r.HZB.BuildUseCompute"), 1,
	TEXT("Selects whether HZB should be built with compute."),
	ECVF_RenderThreadSafe);



BEGIN_SHADER_PARAMETER_STRUCT(FSharedHZBParameters, )
	SHADER_PARAMETER(FVector4, DispatchThreadIdToBufferUV)
	SHADER_PARAMETER(FVector2D, InputViewportMaxBound)
	SHADER_PARAMETER(FVector2D, InvSize)

	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ParentTextureMip)
	SHADER_PARAMETER_SAMPLER(SamplerState, ParentTextureMipSampler)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
END_SHADER_PARAMETER_STRUCT()


class FHZBBuildPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHZBBuildPS);
	SHADER_USE_PARAMETER_STRUCT(FHZBBuildPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSharedHZBParameters, Shared)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32_FLOAT);
	}
};

class FHZBBuildCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHZBBuildCS);
	SHADER_USE_PARAMETER_STRUCT(FHZBBuildCS, FGlobalShader)

	static constexpr int32 kMaxMipBatchSize = 4;

	class FDimFurthest : SHADER_PERMUTATION_BOOL("DIM_FURTHEST");
	class FDimClosest : SHADER_PERMUTATION_BOOL("DIM_CLOSEST");
	class FDimMipLevelCount : SHADER_PERMUTATION_RANGE_INT("DIM_MIP_LEVEL_COUNT", 1, kMaxMipBatchSize);
	using FPermutationDomain = TShaderPermutationDomain<FDimFurthest, FDimClosest, FDimMipLevelCount>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSharedHZBParameters, Shared)

		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D<float>, FurthestHZBOutput, [kMaxMipBatchSize])
		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D<float>, ClosestHZBOutput, [kMaxMipBatchSize])
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		// Necessarily reduce at least closest of furthest.
		if (!PermutationVector.Get<FDimFurthest>() && !PermutationVector.Get<FDimClosest>())
		{
			return false;
		}


		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHZBBuildPS, "/Engine/Private/HZB.usf", "HZBBuildPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FHZBBuildCS, "/Engine/Private/HZB.usf", "HZBBuildCS", SF_Compute);


bool ShouldRenderScreenSpaceDiffuseIndirect(const FViewInfo& View);

static bool RequireClosestDepthHZB(const FViewInfo& View)
{
	return ShouldRenderScreenSpaceDiffuseIndirect(View);
}


void BuildHZB(FRDGBuilder& GraphBuilder, const FSceneTextureParameters& SceneTextures, FViewInfo& View)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BuildHZB);

	FIntPoint HZBSize;
	int32 NumMips;
	{
		const int32 NumMipsX = FMath::Max(FPlatformMath::CeilToInt(FMath::Log2(float(View.ViewRect.Width()))) - 1, 1);
		const int32 NumMipsY = FMath::Max(FPlatformMath::CeilToInt(FMath::Log2(float(View.ViewRect.Height()))) - 1, 1);

		NumMips = FMath::Max(NumMipsX, NumMipsY);

		// Must be power of 2
		HZBSize = FIntPoint(1 << NumMipsX, 1 << NumMipsY);
	}

	bool bReduceClosestDepth = RequireClosestDepthHZB(View);
	bool bUseCompute = bReduceClosestDepth || CVarHZBBuildUseCompute.GetValueOnRenderThread();

	FRDGTextureDesc HZBDesc = FRDGTextureDesc::Create2DDesc(
		HZBSize, PF_R16F,
		FClearValueBinding::None,
		TexCreate_None,
		TexCreate_ShaderResource | (bUseCompute ? TexCreate_UAV : TexCreate_RenderTargetable),
		/* bInForceSeparateTargetAndShaderResource = */ false,
		NumMips);
	HZBDesc.Flags |= GFastVRamConfig.HZB;

	/** Closest and furthest HZB are intentionally in separate render target, because majority of the case you only one or the other.
	 * Keeping them separate avoid doubling the size in cache for this cases, to avoid performance regression.
	 */
	FRDGTextureRef FurthestHZBTexture = GraphBuilder.CreateTexture(HZBDesc, TEXT("HZBFurthest"));

	FRDGTextureRef ClosestHZBTexture = nullptr;
	if (bReduceClosestDepth)
	{
		ClosestHZBTexture = GraphBuilder.CreateTexture(HZBDesc, TEXT("HZBClosest"));
	}

	int32 MaxMipBatchSize = bUseCompute ? FHZBBuildCS::kMaxMipBatchSize : 1;

	auto ReduceMips = [&](
		FRDGTextureSRVRef ParentTextureMip, int32 StartDestMip, FVector4 DispatchThreadIdToBufferUV, FVector2D InputViewportMaxBound,
		bool bOutputClosest, bool bOutputFurthest)
	{
		FIntPoint SrcSize = FIntPoint::DivideAndRoundUp(ParentTextureMip->Desc.Texture->Desc.Extent, 1 << int32(ParentTextureMip->Desc.MipLevel));

		FSharedHZBParameters ShaderParameters;
		ShaderParameters.InvSize = FVector2D(1.0f / SrcSize.X, 1.0f / SrcSize.Y);
		ShaderParameters.InputViewportMaxBound = InputViewportMaxBound;
		ShaderParameters.DispatchThreadIdToBufferUV = DispatchThreadIdToBufferUV;
		ShaderParameters.ParentTextureMip = ParentTextureMip;
		ShaderParameters.ParentTextureMipSampler = TStaticSamplerState<SF_Point>::GetRHI();
		ShaderParameters.View = View.ViewUniformBuffer;

		FIntPoint DstSize = FIntPoint::DivideAndRoundUp(HZBSize, 1 << StartDestMip);

		if (bUseCompute)
		{
			int32 EndDestMip = FMath::Min(StartDestMip + FHZBBuildCS::kMaxMipBatchSize, NumMips);

			FHZBBuildCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHZBBuildCS::FParameters>();
			PassParameters->Shared = ShaderParameters;

			for (int32 DestMip = StartDestMip; DestMip < EndDestMip; DestMip++)
			{
				if (bOutputFurthest)
					PassParameters->FurthestHZBOutput[DestMip - StartDestMip] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FurthestHZBTexture, DestMip));
				if (bOutputClosest)
					PassParameters->ClosestHZBOutput[DestMip - StartDestMip] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ClosestHZBTexture, DestMip));
			}

			FHZBBuildCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FHZBBuildCS::FDimMipLevelCount>(EndDestMip - StartDestMip);
			PermutationVector.Set<FHZBBuildCS::FDimFurthest>(bOutputFurthest);
			PermutationVector.Set<FHZBBuildCS::FDimClosest>(bOutputClosest);

			TShaderMapRef<FHZBBuildCS> ComputeShader(View.ShaderMap, PermutationVector);

			// TODO(RDG): remove ERDGPassFlags::GenerateMips to use FComputeShaderUtils::AddPass().
			ClearUnusedGraphResources(*ComputeShader, PassParameters);
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ReduceHZB(mips=[%d;%d]%s%s) %dx%d",
					StartDestMip, EndDestMip - 1,
					bOutputClosest ? TEXT(" Closest") : TEXT(""),
					bOutputFurthest ? TEXT(" Furthest") : TEXT(""),
					DstSize.X, DstSize.Y),
				PassParameters,
				StartDestMip ? (ERDGPassFlags::Compute | ERDGPassFlags::GenerateMips) : ERDGPassFlags::Compute,
				[PassParameters, ComputeShader, DstSize](FRHICommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, *ComputeShader, *PassParameters, FComputeShaderUtils::GetGroupCount(DstSize, 8));
			});
		}
		else
		{
			check(bOutputFurthest);
			check(!bOutputClosest);

			FHZBBuildPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHZBBuildPS::FParameters>();
			PassParameters->Shared = ShaderParameters;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(FurthestHZBTexture, ERenderTargetLoadAction::ENoAction, StartDestMip);

			TShaderMapRef<FHZBBuildPS> PixelShader(View.ShaderMap);

			// TODO(RDG): remove ERDGPassFlags::GenerateMips to use FPixelShaderUtils::AddFullscreenPass().
			ClearUnusedGraphResources(*PixelShader, PassParameters);
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("DownsampleHZB(mip=%d) %dx%d", StartDestMip, DstSize.X, DstSize.Y),
				PassParameters,
				StartDestMip ? (ERDGPassFlags::Raster | ERDGPassFlags::GenerateMips) : ERDGPassFlags::Raster,
				[PassParameters, &View, PixelShader, DstSize](FRHICommandList& RHICmdList)
			{
				FPixelShaderUtils::DrawFullscreenPixelShader(RHICmdList, View.ShaderMap, *PixelShader, *PassParameters, FIntRect(0, 0, DstSize.X, DstSize.Y));
			});
		}
	};

	// Reduce first mips Closesy and furtherest are done at same time.
	{
		FIntPoint SrcSize = SceneTextures.SceneDepthBuffer->Desc.Extent;

		FRDGTextureSRVRef ParentTextureMip = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneTextures.SceneDepthBuffer));

		FVector4 DispatchThreadIdToBufferUV;
		DispatchThreadIdToBufferUV.X = 2.0f / float(SrcSize.X);
		DispatchThreadIdToBufferUV.Y = 2.0f / float(SrcSize.Y);
		DispatchThreadIdToBufferUV.Z = View.ViewRect.Min.X / float(SrcSize.X);
		DispatchThreadIdToBufferUV.W = View.ViewRect.Min.Y / float(SrcSize.Y);

		FVector2D InputViewportMaxBound = FVector2D(
			float(View.ViewRect.Max.X - 0.5f) / float(SrcSize.X),
			float(View.ViewRect.Max.Y - 0.5f) / float(SrcSize.Y));

		ReduceMips(
			ParentTextureMip,
			/* StartDestMip = */ 0, DispatchThreadIdToBufferUV, InputViewportMaxBound,
			/* bOutputClosest = */ bReduceClosestDepth, /* bOutputFurthest = */ true);
	}

	// Reduce the next mips
	for (int32 StartDestMip = MaxMipBatchSize; StartDestMip < NumMips; StartDestMip += MaxMipBatchSize)
	{
		FIntPoint SrcSize = FIntPoint::DivideAndRoundUp(HZBSize, 1 << int32(StartDestMip - 1));

		FVector4 DispatchThreadIdToBufferUV;
		DispatchThreadIdToBufferUV.X = 2.0f / float(SrcSize.X);
		DispatchThreadIdToBufferUV.Y = 2.0f / float(SrcSize.Y);
		DispatchThreadIdToBufferUV.Z = 0.0f;
		DispatchThreadIdToBufferUV.W = 0.0f;

		FVector2D InputViewportMaxBound(1.0f, 1.0f);
		
		{
			FRDGTextureSRVRef ParentTextureMip = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(FurthestHZBTexture, StartDestMip - 1));
			ReduceMips(ParentTextureMip,
				StartDestMip, DispatchThreadIdToBufferUV, InputViewportMaxBound,
				/* bOutputClosest = */ false, /* bOutputFurthest = */ true);
		}

		if (bReduceClosestDepth)
		{
			check(ClosestHZBTexture)
			FRDGTextureSRVRef ParentTextureMip = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(ClosestHZBTexture, StartDestMip - 1));
			ReduceMips(ParentTextureMip,
				StartDestMip, DispatchThreadIdToBufferUV, InputViewportMaxBound,
				/* bOutputClosest = */ true, /* bOutputFurthest = */ false);
		}
	}

	// Update the view.
	View.HZBMipmap0Size = HZBSize;

	GraphBuilder.QueueTextureExtraction(FurthestHZBTexture, &View.HZB);

	if (ClosestHZBTexture)
		GraphBuilder.QueueTextureExtraction(ClosestHZBTexture, &View.ClosestHZB);
} // BuildHZB()

// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRendering.cpp: Scene rendering.
=============================================================================*/

#include "SceneTextureReductions.h"
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
	SHADER_PARAMETER(FIntVector4, PixelViewPortMinMax)
	SHADER_PARAMETER(FVector2D, InputViewportMaxBound)
	SHADER_PARAMETER(FVector2D, InvSize)

	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ParentTextureMip)
	SHADER_PARAMETER_SAMPLER(SamplerState, ParentTextureMipSampler)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, VisBufferTexture)
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

	class FDimVisBufferFormat : SHADER_PERMUTATION_INT("VIS_BUFFER_FORMAT", 3);
	class FDimFurthest : SHADER_PERMUTATION_BOOL("DIM_FURTHEST");
	class FDimClosest : SHADER_PERMUTATION_BOOL("DIM_CLOSEST");
	class FDimMipLevelCount : SHADER_PERMUTATION_RANGE_INT("DIM_MIP_LEVEL_COUNT", 1, kMaxMipBatchSize);
	using FPermutationDomain = TShaderPermutationDomain<FDimFurthest, FDimClosest, FDimMipLevelCount, FDimVisBufferFormat>;

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


void BuildHZB(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef VisBufferTexture,
	const FIntRect ViewRect,
	FRDGTextureRef* OutClosestHZBTexture,
	FRDGTextureRef* OutFurthestHZBTexture,
	EPixelFormat Format)
{
	RDG_EVENT_SCOPE(GraphBuilder, "BuildHZB");
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BuildHZB);

	FIntPoint HZBSize;
	HZBSize.X = FMath::Max( FPlatformMath::RoundUpToPowerOfTwo( ViewRect.Width() ) >> 1, 1u );
	HZBSize.Y = FMath::Max( FPlatformMath::RoundUpToPowerOfTwo( ViewRect.Height() ) >> 1, 1u );

	int32 NumMips = FMath::Log2( FMath::Max( HZBSize.X, HZBSize.Y ) );

	bool bReduceClosestDepth = OutClosestHZBTexture != nullptr;
	bool bUseCompute = bReduceClosestDepth || CVarHZBBuildUseCompute.GetValueOnRenderThread();

	FRDGTextureDesc HZBDesc = FRDGTextureDesc::Create2D(
		HZBSize, Format,
		FClearValueBinding::None,
		TexCreate_ShaderResource | (bUseCompute ? TexCreate_UAV : TexCreate_RenderTargetable),
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
		FRDGTextureSRVRef ParentTextureMip, FIntPoint SrcSize,
		int32 StartDestMip, FVector4 DispatchThreadIdToBufferUV, FVector2D InputViewportMaxBound,
		FIntVector4 PixelViewPortMinMax, bool bOutputClosest, bool bOutputFurthest)
	{

		FSharedHZBParameters ShaderParameters;
		ShaderParameters.InvSize = FVector2D(1.0f / SrcSize.X, 1.0f / SrcSize.Y);
		ShaderParameters.InputViewportMaxBound = InputViewportMaxBound;
		ShaderParameters.DispatchThreadIdToBufferUV = DispatchThreadIdToBufferUV;
		ShaderParameters.PixelViewPortMinMax = PixelViewPortMinMax;
		ShaderParameters.VisBufferTexture = VisBufferTexture;
		ShaderParameters.ParentTextureMip = ParentTextureMip;
		ShaderParameters.ParentTextureMipSampler = TStaticSamplerState<SF_Point>::GetRHI();

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

			int32 VisBufferFormat = 0;
			if( VisBufferTexture && StartDestMip == 0 )
			{
				VisBufferFormat = VisBufferTexture->Desc.Format == PF_R32_UINT ? 2 : 1;
			}

			FHZBBuildCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FHZBBuildCS::FDimMipLevelCount>(EndDestMip - StartDestMip);
			PermutationVector.Set<FHZBBuildCS::FDimFurthest>(bOutputFurthest);
			PermutationVector.Set<FHZBBuildCS::FDimClosest>(bOutputClosest);
			PermutationVector.Set<FHZBBuildCS::FDimVisBufferFormat>(VisBufferFormat);

			TShaderMapRef<FHZBBuildCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ReduceHZB(mips=[%d;%d]%s%s) %dx%d",
					StartDestMip, EndDestMip - 1,
					bOutputClosest ? TEXT(" Closest") : TEXT(""),
					bOutputFurthest ? TEXT(" Furthest") : TEXT(""),
					DstSize.X, DstSize.Y),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(DstSize, 8));
		}
		else
		{
			check(bOutputFurthest);
			check(!bOutputClosest);

			FHZBBuildPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHZBBuildPS::FParameters>();
			PassParameters->Shared = ShaderParameters;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(FurthestHZBTexture, ERenderTargetLoadAction::ENoAction, StartDestMip);

			FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FHZBBuildPS> PixelShader(GlobalShaderMap);

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				GlobalShaderMap,
				RDG_EVENT_NAME("DownsampleHZB(mip=%d) %dx%d", StartDestMip, DstSize.X, DstSize.Y),
				PixelShader,
				PassParameters,
				FIntRect(0, 0, DstSize.X, DstSize.Y));
		}
	};

	// Reduce first mips Closesy and furtherest are done at same time.
	{
		FRDGTextureSRVRef ParentTextureMip = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneDepth));
		FIntPoint SrcSize = VisBufferTexture ? VisBufferTexture->Desc.Extent : ParentTextureMip->Desc.Texture->Desc.Extent;
		
		FVector4 DispatchThreadIdToBufferUV;
		DispatchThreadIdToBufferUV.X = 2.0f / float(SrcSize.X);
		DispatchThreadIdToBufferUV.Y = 2.0f / float(SrcSize.Y);
		DispatchThreadIdToBufferUV.Z = ViewRect.Min.X / float(SrcSize.X);
		DispatchThreadIdToBufferUV.W = ViewRect.Min.Y / float(SrcSize.Y);

		FVector2D InputViewportMaxBound = FVector2D(
			float(ViewRect.Max.X - 0.5f) / float(SrcSize.X),
			float(ViewRect.Max.Y - 0.5f) / float(SrcSize.Y));
		FIntVector4 PixelViewPortMinMax = FIntVector4(
			ViewRect.Min.X, ViewRect.Min.Y, 
			ViewRect.Max.X - 1, ViewRect.Max.Y - 1);

		ReduceMips(
			ParentTextureMip, SrcSize,
			/* StartDestMip = */ 0, DispatchThreadIdToBufferUV, InputViewportMaxBound, PixelViewPortMinMax,
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
		FIntVector4 PixelViewPortMinMax = FIntVector4(0, 0, SrcSize.X - 1, SrcSize.Y - 1);
		
		{
			FRDGTextureSRVRef ParentTextureMip = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(FurthestHZBTexture, StartDestMip - 1));
			ReduceMips(ParentTextureMip, SrcSize,
				StartDestMip, DispatchThreadIdToBufferUV, InputViewportMaxBound, PixelViewPortMinMax,
				/* bOutputClosest = */ false, /* bOutputFurthest = */ true);
		}

		if (bReduceClosestDepth)
		{
			check(ClosestHZBTexture)
			FRDGTextureSRVRef ParentTextureMip = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(ClosestHZBTexture, StartDestMip - 1));
			ReduceMips(ParentTextureMip, SrcSize,
				StartDestMip, DispatchThreadIdToBufferUV, InputViewportMaxBound, PixelViewPortMinMax,
				/* bOutputClosest = */ true, /* bOutputFurthest = */ false);
		}
	}

	check(OutFurthestHZBTexture);
	*OutFurthestHZBTexture = FurthestHZBTexture;

	if (OutClosestHZBTexture)
	{
		*OutClosestHZBTexture = ClosestHZBTexture;
	}
}

void BuildHZB(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef VisBufferTexture,
	const FViewInfo& View,
	FRDGTextureRef* OutClosestHZBTexture,
	FRDGTextureRef* OutFurthestHZBTexture)
{
	BuildHZB(GraphBuilder, SceneDepth, VisBufferTexture, View.ViewRect, OutClosestHZBTexture, OutFurthestHZBTexture);
}
// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumetricRenderTarget.cpp
=============================================================================*/

#include "VolumetricRenderTarget.h"
#include "DeferredShadingRenderer.h"
#include "RenderCore/Public/RenderGraphUtils.h"
#include "RenderCore/Public/PixelShaderUtils.h"
#include "ScenePrivate.h"
#include "SceneTextureParameters.h"

//PRAGMA_DISABLE_OPTIMIZATION

static TAutoConsoleVariable<int32> CVarVolumetricRenderTarget(
	TEXT("r.VolumetricRenderTarget"), 1,
	TEXT(""),
	ECVF_SetByScalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarVolumetricRenderTargetUvNoiseScale(
	TEXT("r.VolumetricRenderTarget.UvNoiseScale"), 0.5f,
	TEXT(""),
	ECVF_SetByScalability);

static TAutoConsoleVariable<int32> CVarVolumetricRenderTargetMode(
	TEXT("r.VolumetricRenderTarget.Mode"), 0,
	TEXT("0: trace quarter resolution + reconstruct at half resolution + upsample, 1: trace half res + reconstruct full res + upsample, 2: trace at quarter resolution + reconstruct full resolution"),
	ECVF_SetByScalability);

static TAutoConsoleVariable<int32> CVarVolumetricRenderTargetUpsamplingMode(
	TEXT("r.VolumetricRenderTarget.UpsamplingMode"), 3,
	TEXT("0: bilinear, 1: bilinear + jitter, 2: nearest + jitter + depth test, 3: bilinear + jitter + keep closest"),
	ECVF_SetByScalability);

static TAutoConsoleVariable<float> CVarVolumetricRenderTargetTemporalFactor(
	TEXT("r.VolumetricRenderTarget.TemporalFactor"), 0.1f,
	TEXT("This factor control how much the new frame will contribute to the current render, after reprojection constaints."),
	ECVF_SetByScalability);


static bool ShouldPipelineCompileVolumetricRenderTargetShaders(EShaderPlatform ShaderPlatform)
{
	return GetMaxSupportedFeatureLevel(ShaderPlatform) >= ERHIFeatureLevel::SM5;
}

bool ShouldViewRenderVolumetricRenderTarget(const FViewInfo& ViewInfo)
{
	return CVarVolumetricRenderTarget.GetValueOnRenderThread() && ShouldPipelineCompileVolumetricRenderTargetShaders(ViewInfo.GetShaderPlatform())
		&& (ViewInfo.ViewState != nullptr) && !(ViewInfo.bIsReflectionCapture || ViewInfo.bIsSceneCapture);
}

uint32 GetVolumetricRenderTargetMode()
{
	return FMath::Clamp(CVarVolumetricRenderTargetMode.GetValueOnAnyThread(), 0, 2);
}

static bool ShouldViewRenderVolumetricRenderTargetAndTracingValid(const FViewInfo& ViewInfo)
{
	return ShouldViewRenderVolumetricRenderTarget(ViewInfo) && ViewInfo.ViewState->VolumetricRenderTarget.GetVolumetricTracingRTValid();
}

static uint32 GetMainDownsampleFactor()
{
	switch (GetVolumetricRenderTargetMode())
	{
	case 0:
		return 2; // Reconstruct at half resolution of view
		break;
	case 1:
	case 2:
		return 1; // Reconstruct at full resolution of view
		break;
	}
	check(false); // unhandled mode
	return 2;
}

static uint32 GetTraceDownsampleFactor()
{
	switch (GetVolumetricRenderTargetMode())
	{
	case 0:
		return 2; // Trace at half resolution of the view
		break;
	case 1:
		return 2; // Trace at quarter resolution of view (see GetMainDownsampleFactor)
		break;
	case 2:
		return 4; // Trace at quarter resolution of view (see GetMainDownsampleFactor)
		break;
	}
	check(false); // unhandled mode
	return 2;
}

static void GetTextureSafeUvCoordBound(FRDGTextureRef Texture, FUintVector4& TextureValidCoordRect, FVector4& TextureValidUvRect)
{
	FIntVector TexSize = Texture->Desc.GetSize();
	TextureValidCoordRect.X = 0;
	TextureValidCoordRect.Y = 0;
	TextureValidCoordRect.Z = TexSize.X - 1;
	TextureValidCoordRect.W = TexSize.Y - 1;
	TextureValidUvRect.X = 0.51f / float(TexSize.X);
	TextureValidUvRect.Y = 0.51f / float(TexSize.Y);
	TextureValidUvRect.Z = (float(TexSize.X) - 0.51f) / float(TexSize.X);
	TextureValidUvRect.W = (float(TexSize.Y) - 0.51f) / float(TexSize.Y);
};


/*=============================================================================
	UVolumetricCloudComponent implementation.
=============================================================================*/

FVolumetricRenderTargetViewStateData::FVolumetricRenderTargetViewStateData()
	: CurrentRT(1)
	, bFirstTimeUsed(true)
	, bHistoryValid(false)
	, bVolumetricTracingRTValid(false)
	, bVolumetricTracingRTDepthValid(false)
	, VolumetricReconstructRTResolution(FIntPoint::ZeroValue)
	, VolumetricTracingRTResolution(FIntPoint::ZeroValue)
{
	VolumetricReconstructRTDownsampleFactor = GetMainDownsampleFactor();
	VolumetricTracingRTDownsampleFactor = GetTraceDownsampleFactor();
}

FVolumetricRenderTargetViewStateData::~FVolumetricRenderTargetViewStateData()
{
}

void FVolumetricRenderTargetViewStateData::Initialise(FIntPoint& ViewRectResolutionIn)
{
	if (bFirstTimeUsed)
	{
		bFirstTimeUsed = false;
		bHistoryValid = false;
		FrameId = 0;
		NoiseFrameIndex = 0;
		NoiseFrameIndexModPattern = 0;
		CurrentPixelOffset = FIntPoint::ZeroValue;
	}

	{

		CurrentRT = 1 - CurrentRT;
		const uint32 PreviousRT = 1 - CurrentRT;

		// We always reallocate on a resolution change to adapt to dynamic resolution scaling.
		// TODO allocate once at max resolution and change source and destination coord/uvs/rect.
		if (FullResolution != ViewRectResolutionIn || GetMainDownsampleFactor() != VolumetricReconstructRTDownsampleFactor || GetTraceDownsampleFactor() != VolumetricTracingRTDownsampleFactor)
		{
			VolumetricReconstructRTDownsampleFactor = GetMainDownsampleFactor();
			VolumetricTracingRTDownsampleFactor = GetTraceDownsampleFactor();

			FullResolution = ViewRectResolutionIn;
			VolumetricReconstructRTResolution = FIntPoint::DivideAndRoundUp(FullResolution, VolumetricReconstructRTDownsampleFactor);							// Half resolution
			VolumetricTracingRTResolution = FIntPoint::DivideAndRoundUp(VolumetricReconstructRTResolution, VolumetricTracingRTDownsampleFactor);	// Half resolution of the volumetric buffer

			// Need a new size so release the low resolution trace buffer
			VolumetricTracingRT.SafeRelease();
			VolumetricTracingRTDepth.SafeRelease();
		}

		FIntVector CurrentTargetResVec = VolumetricReconstructRT[CurrentRT].IsValid() ? VolumetricReconstructRT[CurrentRT]->GetDesc().GetSize() : FIntVector::ZeroValue;
		FIntPoint CurrentTargetRes = FIntPoint::DivideAndRoundUp(FullResolution, VolumetricReconstructRTDownsampleFactor);
		if (VolumetricReconstructRT[CurrentRT].IsValid() && FIntPoint(CurrentTargetResVec.X, CurrentTargetResVec.Y) != CurrentTargetRes)
		{
			// Resolution does not match so release target we are going to render in
			VolumetricReconstructRT[CurrentRT].SafeRelease();
			VolumetricReconstructRTDepth[CurrentRT].SafeRelease();
		}

		// Regular every frame update
		{
			// Do not mark history as valid if the half resolution buffer is not valid. That means nothing has been rendered last frame.
			// That can happen when cloud is used to render into that buffer
			bHistoryValid = VolumetricReconstructRT[PreviousRT].IsValid();

			NoiseFrameIndex += FrameId == 0 ? 1 : 0;
			NoiseFrameIndexModPattern = NoiseFrameIndex % (VolumetricTracingRTDownsampleFactor * VolumetricTracingRTDownsampleFactor);

			FrameId++;
			FrameId = FrameId % (VolumetricTracingRTDownsampleFactor * VolumetricTracingRTDownsampleFactor);

			if (VolumetricTracingRTDownsampleFactor == 2)
			{
				static int32 OrderDithering2x2[4] = { 0, 2, 3, 1 };
				int32 LocalFrameId = OrderDithering2x2[FrameId];
				CurrentPixelOffset = FIntPoint(LocalFrameId % VolumetricTracingRTDownsampleFactor, LocalFrameId / VolumetricTracingRTDownsampleFactor);
			}
			else if (VolumetricTracingRTDownsampleFactor == 4)
			{
				static int32 OrderDithering4x4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
				int32 LocalFrameId = OrderDithering4x4[FrameId];
				CurrentPixelOffset = FIntPoint(LocalFrameId % VolumetricTracingRTDownsampleFactor, LocalFrameId / VolumetricTracingRTDownsampleFactor);
			}
			else
			{
				// Default linear parse
				CurrentPixelOffset = FIntPoint(FrameId % VolumetricTracingRTDownsampleFactor, FrameId / VolumetricTracingRTDownsampleFactor);
			}
		}
	}

	bVolumetricTracingRTValid = false;
	bVolumetricTracingRTDepthValid = false;
}

FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateVolumetricTracingRT(FRDGBuilder& GraphBuilder)
{
	check(FullResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	if (VolumetricTracingRT.IsValid())
	{
		return GraphBuilder.RegisterExternalTexture(VolumetricTracingRT);
	}

	FRDGTextureRef RDGVolumetricTracingRT = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2DDesc(VolumetricTracingRTResolution, PF_FloatRGBA, FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)),
			TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false, 1), TEXT("RDGVolumetricTracingRT"));
	return RDGVolumetricTracingRT;
}

FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateVolumetricTracingRTDepth(FRDGBuilder& GraphBuilder)
{
	check(FullResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	if (VolumetricTracingRTDepth.IsValid())
	{
		return GraphBuilder.RegisterExternalTexture(VolumetricTracingRTDepth);
	}

	FRDGTextureRef RDGVolumetricTracingRTDepth = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2DDesc(VolumetricTracingRTResolution, PF_R16F, FClearValueBinding(FLinearColor(63000.0f, 63000.0f, 63000.0f, 63000.0f)),
			TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false, 1), TEXT("RDGVolumetricTracingRTDepth"));
	return RDGVolumetricTracingRTDepth;
}

void FVolumetricRenderTargetViewStateData::ExtractToVolumetricTracingRT(FRDGBuilder& GraphBuilder, FRDGTextureRef RDGVolumetricTracingRT)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	bVolumetricTracingRTValid = true;
	GraphBuilder.QueueTextureExtraction(RDGVolumetricTracingRT, &VolumetricTracingRT);
}

void FVolumetricRenderTargetViewStateData::ExtractToVolumetricTracingRTDepth(FRDGBuilder& GraphBuilder, FRDGTextureRef RDGVolumetricTracingRTDepth)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	bVolumetricTracingRTDepthValid = true;
	GraphBuilder.QueueTextureExtraction(RDGVolumetricTracingRTDepth, &VolumetricTracingRTDepth);
}


FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateDstVolumetricReconstructRT(FRDGBuilder& GraphBuilder)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	if (VolumetricReconstructRT[CurrentRT].IsValid())
	{
		return GraphBuilder.RegisterExternalTexture(VolumetricReconstructRT[CurrentRT]);
	}

	FRDGTextureRef RDGVolumetricVolumetricReconstructRTRT = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2DDesc(VolumetricReconstructRTResolution, PF_FloatRGBA, FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)),
		TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false, 1), TEXT("RDGVolumetricVolumetricReconstructRTRT"));
	return RDGVolumetricVolumetricReconstructRTRT;
}


FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateDstVolumetricReconstructRTDepth(FRDGBuilder& GraphBuilder)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	if (VolumetricReconstructRTDepth[CurrentRT].IsValid())
	{
		return GraphBuilder.RegisterExternalTexture(VolumetricReconstructRTDepth[CurrentRT]);
	}

	FRDGTextureRef RDGVolumetricVolumetricReconstructRTRTDepth = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2DDesc(VolumetricReconstructRTResolution, PF_R16F, FClearValueBinding(FLinearColor(63000.0f, 63000.0f, 63000.0f, 63000.0f)),
		TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false, 1), TEXT("RDGVolumetricVolumetricReconstructRTRTDepth"));
	return RDGVolumetricVolumetricReconstructRTRTDepth;
}

void FVolumetricRenderTargetViewStateData::ExtractDstVolumetricReconstructRT(FRDGBuilder& GraphBuilder, FRDGTextureRef RDGVolumetricVolumetricReconstructRT)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	GraphBuilder.QueueTextureExtraction(RDGVolumetricVolumetricReconstructRT, &VolumetricReconstructRT[CurrentRT]);
}

void FVolumetricRenderTargetViewStateData::ExtractDstVolumetricReconstructRTDepth(FRDGBuilder& GraphBuilder, FRDGTextureRef RDGVolumetricRTDepth)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	GraphBuilder.QueueTextureExtraction(RDGVolumetricRTDepth, &VolumetricReconstructRTDepth[CurrentRT]);
}

FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateSrcVolumetricReconstructRT(FRDGBuilder& GraphBuilder)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	check(VolumetricReconstructRT[1u - CurrentRT].IsValid());
	return GraphBuilder.RegisterExternalTexture(VolumetricReconstructRT[1u - CurrentRT]);
}

FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateSrcVolumetricReconstructRTDepth(FRDGBuilder& GraphBuilder)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	check(VolumetricReconstructRT[1u - CurrentRT].IsValid());
	return GraphBuilder.RegisterExternalTexture(VolumetricReconstructRTDepth[1u - CurrentRT]);
}

FUintVector4 FVolumetricRenderTargetViewStateData::GetTracingToFullResResolutionScaleBias() const
{
	// This is used to sample full res data such as depth and avoid extra downsampling for now...
	const uint32 CombinedDownsampleFactor = VolumetricReconstructRTDownsampleFactor * VolumetricTracingRTDownsampleFactor;
	return FUintVector4( CombinedDownsampleFactor, CombinedDownsampleFactor,										// Scale is the combined downsample factor
		CurrentPixelOffset.X * VolumetricReconstructRTDownsampleFactor, CurrentPixelOffset.Y * VolumetricReconstructRTDownsampleFactor);	// Each sample will then sample from full res according to reconstructed RT offset times its downsample factor
}


/*=============================================================================
	FSceneRenderer implementation.
=============================================================================*/

void FSceneRenderer::InitVolumetricRenderTargetForViews(FRHICommandListImmediate& RHICmdList)
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& ViewInfo = Views[ViewIndex];
		if (!ShouldViewRenderVolumetricRenderTarget(ViewInfo))
		{
			continue;
		}
		FVolumetricRenderTargetViewStateData& VolumetricRT = ViewInfo.ViewState->VolumetricRenderTarget;


		//FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		FIntPoint ViewRect = ViewInfo.ViewRect.Size();
		VolumetricRT.Initialise(ViewRect);	// TODO this is going to reallocate a buffer each time dynamic resolution scaling is applied

		FViewUniformShaderParameters ViewVolumetricRTParameters = *ViewInfo.CachedViewUniformShaderParameters;
		{
			const FIntPoint& VolumetricReconstructResolution = VolumetricRT.GetCurrentVolumetricReconstructRTResolution();
			const FIntPoint& VolumetricTracingResolution = VolumetricRT.GetCurrentVolumetricTracingRTResolution();
			const FIntPoint& CurrentPixelOffset = VolumetricRT.GetCurrentTracingPixelOffset();
			const uint32 VolumetricReconstructRTDownSample = VolumetricRT.GetVolumetricReconstructRTDownsampleFactor();
			const uint32 VolumetricTracingRTDownSample = VolumetricRT.GetVolumetricTracingRTDownsampleFactor();

			// We jitter and reconstruct the volumetric view before TAA so we do not want any of its jitter.
			// We do use TAA remove bilinear artifact at up sampling time.
			FViewMatrices ViewMatrices = ViewInfo.ViewMatrices;
			ViewMatrices.HackRemoveTemporalAAProjectionJitter();

			float DownSampleFactor = float(VolumetricReconstructRTDownSample * VolumetricTracingRTDownSample);

			// Offset to the correct half resolution pixel
			FVector2D CenterCoord = FVector2D(VolumetricReconstructRTDownSample / 2.0f);
			FVector2D TargetCoord = FVector2D(CurrentPixelOffset) + FVector2D(0.5f, 0.5f);
			FVector2D OffsetCoord = (TargetCoord - CenterCoord) * (FVector2D(-2.0f, 2.0f) / FVector2D(VolumetricReconstructResolution));
			ViewMatrices.HackAddTemporalAAProjectionJitter(OffsetCoord);

			ViewInfo.SetupViewRectUniformBufferParameters(
				ViewVolumetricRTParameters,
				VolumetricTracingResolution,
				FIntRect(0, 0, VolumetricTracingResolution.X, VolumetricTracingResolution.Y),
				ViewMatrices,
				ViewInfo.PrevViewInfo.ViewMatrices // This could also be changed if needed
			);
		}
		ViewInfo.VolumetricRenderTargetViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(ViewVolumetricRTParameters, UniformBuffer_SingleFrame);
	}
}

//////////////////////////////////////////////////////////////////////////

class FReconstructVolumetricRenderTargetPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReconstructVolumetricRenderTargetPS);
	SHADER_USE_PARAMETER_STRUCT(FReconstructVolumetricRenderTargetPS, FGlobalShader);

	class FHistoryAvailable : SHADER_PERMUTATION_BOOL("PERMUTATION_HISTORY_AVAILABLE");
	using FPermutationDomain = TShaderPermutationDomain<FHistoryAvailable>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TracingVolumetricTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TracingVolumetricDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousFrameVolumetricTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousFrameVolumetricDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearTextureSampler)
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER(FVector4, DstVolumetricTextureSizeAndInvSize)
		SHADER_PARAMETER(FVector4, PreviousVolumetricTextureSizeAndInvSize)
		SHADER_PARAMETER(FIntPoint, CurrentTracingPixelOffset)
		SHADER_PARAMETER(int32, DownSampleFactor)
		SHADER_PARAMETER(int32, VolumetricRenderTargetMode)
		SHADER_PARAMETER(FUintVector4, TracingVolumetricTextureValidCoordRect)
		SHADER_PARAMETER(FVector4, TracingVolumetricTextureValidUvRect)
		SHADER_PARAMETER(FUintVector4, PreviousFrameVolumetricTextureValidCoordRect)
		SHADER_PARAMETER(FVector4, PreviousFrameVolumetricTextureValidUvRect)
		SHADER_PARAMETER(float, TemporalFactor)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileVolumetricRenderTargetShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_RECONSTRUCT_VOLUMETRICRT"), TEXT("1"));
	}
};

IMPLEMENT_GLOBAL_SHADER(FReconstructVolumetricRenderTargetPS, "/Engine/Private/VolumetricRenderTarget.usf", "ReconstructVolumetricRenderTargetPS", SF_Pixel);

//////////////////////////////////////////////////////////////////////////

void FSceneRenderer::ReconstructVolumetricRenderTarget(FRHICommandListImmediate& RHICmdList)
{
	bool bAnyViewRequiresProcessing = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& ViewInfo = Views[ViewIndex];
		bAnyViewRequiresProcessing |= ShouldViewRenderVolumetricRenderTargetAndTracingValid(ViewInfo);
	}
	if (!bAnyViewRequiresProcessing)
	{
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdList);
	FRDGTextureRef BlackDummy = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& ViewInfo = Views[ViewIndex];
		if (!ShouldViewRenderVolumetricRenderTargetAndTracingValid(ViewInfo))
		{
			continue;
		}
		FVolumetricRenderTargetViewStateData& VolumetricRT = ViewInfo.ViewState->VolumetricRenderTarget;

		FRDGTextureRef DstVolumetric = VolumetricRT.GetOrCreateDstVolumetricReconstructRT(GraphBuilder);
		FRDGTextureRef DstVolumetricDepth = VolumetricRT.GetOrCreateDstVolumetricReconstructRTDepth(GraphBuilder);
		FRDGTextureRef SrcTracingVolumetric = VolumetricRT.GetOrCreateVolumetricTracingRT(GraphBuilder);
		FRDGTextureRef SrcTracingVolumetricDepth = VolumetricRT.GetOrCreateVolumetricTracingRTDepth(GraphBuilder);
		FRDGTextureRef PreviousFrameVolumetricTexture = VolumetricRT.GetHistoryValid() ? VolumetricRT.GetOrCreateSrcVolumetricReconstructRT(GraphBuilder) : BlackDummy;
		FRDGTextureRef PreviousFrameVolumetricDepthTexture = VolumetricRT.GetHistoryValid() ? VolumetricRT.GetOrCreateSrcVolumetricReconstructRTDepth(GraphBuilder) : BlackDummy;

		const uint32 TracingVolumetricRTDownSample = VolumetricRT.GetVolumetricTracingRTDownsampleFactor();

		FReconstructVolumetricRenderTargetPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FReconstructVolumetricRenderTargetPS::FHistoryAvailable>(VolumetricRT.GetHistoryValid());
		TShaderMapRef<FReconstructVolumetricRenderTargetPS> PixelShader(ViewInfo.ShaderMap, PermutationVector);

		FReconstructVolumetricRenderTargetPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReconstructVolumetricRenderTargetPS::FParameters>();
		PassParameters->ViewUniformBuffer = ViewInfo.VolumetricRenderTargetViewUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(DstVolumetric, ERenderTargetLoadAction::ENoAction);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(DstVolumetricDepth, ERenderTargetLoadAction::ENoAction);
		PassParameters->TracingVolumetricTexture = SrcTracingVolumetric;
		PassParameters->TracingVolumetricDepthTexture = SrcTracingVolumetricDepth;
		PassParameters->PreviousFrameVolumetricTexture = PreviousFrameVolumetricTexture;
		PassParameters->PreviousFrameVolumetricDepthTexture = PreviousFrameVolumetricDepthTexture;
		PassParameters->LinearTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->CurrentTracingPixelOffset = VolumetricRT.GetCurrentTracingPixelOffset();
		PassParameters->DownSampleFactor = TracingVolumetricRTDownSample;
		PassParameters->VolumetricRenderTargetMode = GetVolumetricRenderTargetMode();
		GetTextureSafeUvCoordBound(SrcTracingVolumetric, PassParameters->TracingVolumetricTextureValidCoordRect, PassParameters->TracingVolumetricTextureValidUvRect);
		GetTextureSafeUvCoordBound(PreviousFrameVolumetricTexture, PassParameters->PreviousFrameVolumetricTextureValidCoordRect, PassParameters->PreviousFrameVolumetricTextureValidUvRect);
		PassParameters->TemporalFactor = FMath::Clamp(CVarVolumetricRenderTargetTemporalFactor.GetValueOnAnyThread(), 0.0f, 1.0f);

		FIntVector DstVolumetricSize = DstVolumetric->Desc.GetSize();
		FVector2D DstVolumetricTextureSize = FVector2D(float(DstVolumetricSize.X), float(DstVolumetricSize.Y));
		FVector2D PreviousVolumetricTextureSize = FVector2D(float(PreviousFrameVolumetricTexture->Desc.GetSize().X), float(PreviousFrameVolumetricTexture->Desc.GetSize().Y));
		PassParameters->DstVolumetricTextureSizeAndInvSize = FVector4(DstVolumetricTextureSize.X, DstVolumetricTextureSize.Y, 1.0f / DstVolumetricTextureSize.X, 1.0f / DstVolumetricTextureSize.Y);
		PassParameters->PreviousVolumetricTextureSizeAndInvSize = FVector4(PreviousVolumetricTextureSize.X, PreviousVolumetricTextureSize.Y, 1.0f / PreviousVolumetricTextureSize.X, 1.0f / PreviousVolumetricTextureSize.Y);

		FPixelShaderUtils::AddFullscreenPass<FReconstructVolumetricRenderTargetPS>(
			GraphBuilder, ViewInfo.ShaderMap, RDG_EVENT_NAME("VolumetricReconstruct"), PixelShader, PassParameters, 
			FIntRect(0, 0, DstVolumetricSize.X, DstVolumetricSize.Y));

		VolumetricRT.ExtractDstVolumetricReconstructRT(GraphBuilder, DstVolumetric);
		VolumetricRT.ExtractDstVolumetricReconstructRTDepth(GraphBuilder, DstVolumetricDepth);
	}

	GraphBuilder.Execute();

	// Compose over the scene color.
	ComposeVolumetricRenderTargetOverScene(RHICmdList);
}

//////////////////////////////////////////////////////////////////////////

class FComposeVolumetricRTOverScenePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComposeVolumetricRTOverScenePS);
	SHADER_USE_PARAMETER_STRUCT(FComposeVolumetricRTOverScenePS, FGlobalShader);

	class FUpsamplingMode : SHADER_PERMUTATION_RANGE_INT("PERMUTATION_UPSAMPLINGMODE", 0, 4);
	using FPermutationDomain = TShaderPermutationDomain<FUpsamplingMode>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VolumetricTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VolumetricDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthBuffer)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearTextureSampler)
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER(float, UvOffsetScale)
		SHADER_PARAMETER(FVector4, VolumetricTextureSizeAndInvSize)
		SHADER_PARAMETER(FVector2D, FullResolutionToVolumetricBufferResolution)
		SHADER_PARAMETER(FUintVector4, VolumetricTextureValidCoordRect)
		SHADER_PARAMETER(FVector4, VolumetricTextureValidUvRect)
	END_SHADER_PARAMETER_STRUCT()

		static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileVolumetricRenderTargetShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_COMPOSE_VOLUMETRICRT"), TEXT("1"));
	}
};

IMPLEMENT_GLOBAL_SHADER(FComposeVolumetricRTOverScenePS, "/Engine/Private/VolumetricRenderTarget.usf", "ComposeVolumetricRTOverScenePS", SF_Pixel);

//////////////////////////////////////////////////////////////////////////

void FSceneRenderer::ComposeVolumetricRenderTargetOverScene(FRHICommandListImmediate& RHICmdList)
{
	// This is called from ReconstructVolumetricRenderTarget so no need to check if any views need the process
	FRDGBuilder GraphBuilder(RHICmdList);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FRDGTextureRef SceneColor = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("SceneColor"));
	TRefCountPtr<IPooledRenderTarget> SceneDepthZ = SceneContext.SceneDepthZ;

	FRHIBlendState* PreMultipliedColorTransmittanceBlend = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& ViewInfo = Views[ViewIndex];
		if (!ShouldViewRenderVolumetricRenderTarget(ViewInfo))
		{
			continue;
		}
		FVolumetricRenderTargetViewStateData& VolumetricRT = ViewInfo.ViewState->VolumetricRenderTarget;
		FRDGTextureRef VolumetricTexture = VolumetricRT.GetOrCreateDstVolumetricReconstructRT(GraphBuilder);
		FRDGTextureRef VolumetricDepthTexture = VolumetricRT.GetOrCreateDstVolumetricReconstructRTDepth(GraphBuilder);

		int UpsamplingMode = FMath::Clamp(CVarVolumetricRenderTargetUpsamplingMode.GetValueOnAnyThread(), 0, 3);

		// When reconstructed and back buffer resolution matches, force using a pixel perfect upsampling.
		uint32 VRTMode = GetVolumetricRenderTargetMode();
		UpsamplingMode = UpsamplingMode == 3 && (VRTMode == 1 || VRTMode == 2) ? 2 : UpsamplingMode;

		FComposeVolumetricRTOverScenePS::FPermutationDomain PermutationVector; 
		PermutationVector.Set<FComposeVolumetricRTOverScenePS::FUpsamplingMode>(UpsamplingMode);
		TShaderMapRef<FComposeVolumetricRTOverScenePS> PixelShader(ViewInfo.ShaderMap, PermutationVector);

		FComposeVolumetricRTOverScenePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComposeVolumetricRTOverScenePS::FParameters>();
		PassParameters->ViewUniformBuffer = ViewInfo.ViewUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
		PassParameters->VolumetricTexture = VolumetricTexture;
		PassParameters->VolumetricDepthTexture = VolumetricDepthTexture;
		PassParameters->SceneDepthBuffer = GraphBuilder.RegisterExternalTexture(SceneDepthZ);
		PassParameters->LinearTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->UvOffsetScale = CVarVolumetricRenderTargetUvNoiseScale.GetValueOnAnyThread();
		PassParameters->FullResolutionToVolumetricBufferResolution = FVector2D(1.0f / float(GetMainDownsampleFactor()), float(GetMainDownsampleFactor()));
		GetTextureSafeUvCoordBound(PassParameters->VolumetricTexture, PassParameters->VolumetricTextureValidCoordRect, PassParameters->VolumetricTextureValidUvRect);

		FVector2D VolumetricTextureSize = FVector2D(float(VolumetricTexture->Desc.GetSize().X), float(VolumetricTexture->Desc.GetSize().Y));
		PassParameters->VolumetricTextureSizeAndInvSize = FVector4(VolumetricTextureSize.X, VolumetricTextureSize.Y, 1.0f / VolumetricTextureSize.X, 1.0f / VolumetricTextureSize.Y);

		FPixelShaderUtils::AddFullscreenPass<FComposeVolumetricRTOverScenePS>(
			GraphBuilder, ViewInfo.ShaderMap, RDG_EVENT_NAME("VolumetricComposeOverScene"), PixelShader, PassParameters, ViewInfo.ViewRect,
			PreMultipliedColorTransmittanceBlend);
	}

	GraphBuilder.Execute();
}



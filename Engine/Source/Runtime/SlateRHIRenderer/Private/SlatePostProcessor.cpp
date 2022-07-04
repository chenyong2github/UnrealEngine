// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlatePostProcessor.h"
#include "SlatePostProcessResource.h"
#include "SlateShaders.h"
#include "ScreenRendering.h"
#include "SceneUtils.h"
#include "RendererInterface.h"
#include "StaticBoundShaderState.h"
#include "PipelineStateCache.h"
#include "CommonRenderResources.h"
#include "HDRHelper.h"
#include "RendererUtils.h"

DECLARE_CYCLE_STAT(TEXT("Slate PostProcessing RT"), STAT_SlatePostProcessingRTTime, STATGROUP_Slate);
DECLARE_CYCLE_STAT(TEXT("Slate ColorDeficiency RT"), STAT_SlateColorDeficiencyRTTime, STATGROUP_Slate);

FSlatePostProcessor::FSlatePostProcessor()
{
	const int32 NumIntermediateTargets = 2;
	IntermediateTargets = new FSlatePostProcessResource(NumIntermediateTargets);
	BeginInitResource(IntermediateTargets);
}

FSlatePostProcessor::~FSlatePostProcessor()
{
	// Note this is deleted automatically because it implements FDeferredCleanupInterface.
	IntermediateTargets->CleanUp();
}


// Pixel shader to composite UI over HDR buffer
class FBlitUIToHDRPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBlitUIToHDRPS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && (RHISupportsGeometryShaders(Parameters.Platform) || RHISupportsVertexShaderLayer(Parameters.Platform));
	}

	FBlitUIToHDRPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		UITexture.Bind(Initializer.ParameterMap, TEXT("UITexture"));
		UIWriteMaskTexture.Bind(Initializer.ParameterMap, TEXT("UIWriteMaskTexture"));
		UISampler.Bind(Initializer.ParameterMap, TEXT("UISampler"));
		UILevel.Bind(Initializer.ParameterMap, TEXT("UILevel"));
		SrgbToOutputMatrix.Bind(Initializer.ParameterMap, TEXT("SrgbToOutputMatrix"));
	}
	FBlitUIToHDRPS() {}

	void SetParameters(FRHICommandList& RHICmdList, FRHITexture* UITextureRHI, FRHITexture* UITextureWriteMaskRHI, const FMatrix44f& InSrgbToOutputMatrix)
	{
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), UITexture, UISampler, TStaticSamplerState<SF_Point>::GetRHI(), UITextureRHI);
		static auto CVarHDRUILevel = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HDR.UI.Level"));
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), UILevel, CVarHDRUILevel ? CVarHDRUILevel->GetFloat() : 1.0f);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), SrgbToOutputMatrix, InSrgbToOutputMatrix);
		if (UITextureWriteMaskRHI != nullptr)
		{
			SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), UIWriteMaskTexture, UITextureWriteMaskRHI);
		}
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("BLIT_UI_TO_HDR"), 1);
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/CompositeUIPixelShader.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("BlitUIToHDRPS");
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, UITexture);
	LAYOUT_FIELD(FShaderResourceParameter, UISampler);
	LAYOUT_FIELD(FShaderResourceParameter, UIWriteMaskTexture);
	LAYOUT_FIELD(FShaderParameter, SrgbToOutputMatrix);
	LAYOUT_FIELD(FShaderParameter, UILevel);
};

IMPLEMENT_SHADER_TYPE(, FBlitUIToHDRPS, FBlitUIToHDRPS::GetSourceFilename(), FBlitUIToHDRPS::GetFunctionName(), SF_Pixel);

static void BlitUIToHDRScene(FRHICommandListImmediate& RHICmdList, IRendererModule& RendererModule, const FPostProcessRectParams& RectParams)
{
	SCOPED_DRAW_EVENT(RHICmdList, SlatePostProcessBlitUIToHDR);

	FRHITexture* SourceTexture = RectParams.UITarget->GetRHI();

	RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVMask));
	TRefCountPtr<IPooledRenderTarget> UITargetRTMask;
	if (RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform))
	{
		const auto FeatureLevel = GMaxRHIFeatureLevel;
		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

		IPooledRenderTarget* RenderTargets[] = { RectParams.UITarget.GetReference() };
		FRenderTargetWriteMask::Decode(RHICmdList, ShaderMap, RenderTargets, UITargetRTMask, TexCreate_None, TEXT("UIRTWriteMask"));
	}

	// Source is the viewport.  This is the width and height of the viewport backbuffer
	const int32 SrcTextureWidth = RectParams.SourceTextureSize.X;
	const int32 SrcTextureHeight = RectParams.SourceTextureSize.Y;

	// Rect of the viewport
	const FSlateRect& SourceRect = RectParams.SourceRect;

	// Rect of the final destination post process effect (not downsample rect).  This is the area we sample from
	const FSlateRect& DestRect = RectParams.DestRect;

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);

	FTexture2DRHIRef DestTexture = RectParams.SourceTexture;

	TShaderMapRef<FBlitUIToHDRPS> PixelShader(ShaderMap);

	RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

	const FVector2f InvSrcTextureSize(1.f / SrcTextureWidth, 1.f / SrcTextureHeight);

	// Add some guard band to ensure blur will reach these pixels. It will overwrite pixels below, but these are going to be composited at the end anyway
	const FVector2f UVStart = FVector2f(DestRect.Left - 10.0f, DestRect.Top - 10.0f) * InvSrcTextureSize;
	const FVector2f UVEnd = FVector2f(DestRect.Right + 10.0f, DestRect.Bottom + 10.0f) * InvSrcTextureSize;
	const FVector2f SizeUV = UVEnd - UVStart;

	RHICmdList.SetViewport(0, 0, 0, SrcTextureWidth, SrcTextureHeight, 0.0f);
	RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

	FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("BlitUIToHDR"));
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

		FRHITexture* UITargetRTMaskTexture = RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform) ? UITargetRTMask->GetRHI() : nullptr;

		FMatrix44f Srgb_2_XYZ = GamutToXYZMatrix(EDisplayColorGamut::sRGB_D65);
		FMatrix44f XYZ_2_Output = XYZToGamutMatrix(RectParams.HDRDisplayColorGamut);
		// note: we use mul(m,v) instead of mul(v,m) in the shaders for color conversions which is why matrix multiplication is reversed compared to what we usually do
		FMatrix44f CombinedMatrix = XYZ_2_Output * Srgb_2_XYZ;

		PixelShader->SetParameters(RHICmdList, SourceTexture, UITargetRTMaskTexture, CombinedMatrix);

		RendererModule.DrawRectangle(
			RHICmdList,
			UVStart.X * SrcTextureWidth, UVStart.Y * SrcTextureHeight,
			SizeUV.X * SrcTextureWidth, SizeUV.Y * SrcTextureHeight,
			UVStart.X, UVStart.Y,
			SizeUV.X, SizeUV.Y,
			FIntPoint(SrcTextureWidth, SrcTextureHeight),
			FIntPoint(1, 1),
			VertexShader,
			EDRF_Default);
	}
	RHICmdList.EndRenderPass();
}

void FSlatePostProcessor::BlurRect(FRHICommandListImmediate& RHICmdList, IRendererModule& RendererModule, const FBlurRectParams& Params, const FPostProcessRectParams& RectParams)
{
	SCOPE_CYCLE_COUNTER(STAT_SlatePostProcessingRTTime);
	check(RHICmdList.IsOutsideRenderPass());

	TArray<FVector4f> WeightsAndOffsets;
	const int32 SampleCount = ComputeBlurWeights(Params.KernelSize, Params.Strength, WeightsAndOffsets);


	const bool bDownsample = Params.DownsampleAmount > 0;

	FIntPoint DestRectSize = RectParams.DestRect.GetSize().IntPoint();
	FIntPoint RequiredSize = bDownsample
										? FIntPoint(FMath::DivideAndRoundUp(DestRectSize.X, Params.DownsampleAmount), FMath::DivideAndRoundUp(DestRectSize.Y, Params.DownsampleAmount))
										: DestRectSize;

	// The max size can get ridiculous with large scale values.  Clamp to size of the backbuffer
	RequiredSize.X = FMath::Min(RequiredSize.X, RectParams.SourceTextureSize.X);
	RequiredSize.Y = FMath::Min(RequiredSize.Y, RectParams.SourceTextureSize.Y);
	
	SCOPED_DRAW_EVENTF(RHICmdList, SlatePostProcess, TEXT("Slate Post Process Blur Background Kernel: %dx%d Size: %dx%d"), SampleCount, SampleCount, RequiredSize.X, RequiredSize.Y);


	const FIntPoint DownsampleSize = RequiredSize;

	IntermediateTargets->Update(RequiredSize, RectParams.SourceTexture);

	if (RectParams.UITarget.IsValid() && RectParams.UITarget->GetRHI() != RectParams.SourceTexture.GetReference())
	{
		// in HDR mode, we are going to blur SourceTexture, but still need to take into account the UI already rendered. Blit UI into HDR target
		BlitUIToHDRScene(RHICmdList, RendererModule, RectParams);
	}

	if(bDownsample)
	{
		DownsampleRect(RHICmdList, RendererModule, RectParams, DownsampleSize);
	}

	FSamplerStateRHIRef BilinearClamp = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

#if 1
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	check(ShaderMap);

	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<FSlatePostProcessBlurPS> PixelShader(ShaderMap);

	const int32 SrcTextureWidth = RectParams.SourceTextureSize.X;
	const int32 SrcTextureHeight = RectParams.SourceTextureSize.Y;

	const int32 DestTextureWidth = IntermediateTargets->GetWidth();
	const int32 DestTextureHeight = IntermediateTargets->GetHeight();

	const FSlateRect& SourceRect = RectParams.SourceRect;
	const FSlateRect& DestRect = RectParams.DestRect;

	FVertexDeclarationRHIRef VertexDecl = GFilterVertexDeclaration.VertexDeclarationRHI;
	check(IsValidRef(VertexDecl));
	
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
	RHICmdList.SetViewport(0.f, 0.f, 0.f, (float)DestTextureWidth, (float)DestTextureHeight, 0.0f);
	
	const FVector2f InvBufferSize = FVector2f(1.0f / (float)DestTextureWidth, 1.0f / (float)DestTextureHeight);
	const FVector2f HalfTexelOffset = FVector2f(0.5f/ (float)DestTextureWidth, 0.5f/ (float)DestTextureHeight);

	for (int32 PassIndex = 0; PassIndex < 2; ++PassIndex)
	{
		// First pass render to the render target with the post process fx
		if (PassIndex == 0)
		{
			FTexture2DRHIRef SourceTexture = bDownsample ? IntermediateTargets->GetRenderTarget(0) : RectParams.SourceTexture;
			FTexture2DRHIRef DestTexture = IntermediateTargets->GetRenderTarget(1);

			RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
			RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("SlateBlurRectPass0"));
			{
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = VertexDecl;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				PixelShader->SetWeightsAndOffsets(RHICmdList, WeightsAndOffsets, SampleCount);
				PixelShader->SetTexture(RHICmdList, SourceTexture, BilinearClamp);

				if (bDownsample)
				{
					PixelShader->SetUVBounds(RHICmdList, FVector4f(FVector2f::ZeroVector, FVector2f((float)DownsampleSize.X / DestTextureWidth, (float)DownsampleSize.Y / DestTextureHeight) - HalfTexelOffset));
					PixelShader->SetBufferSizeAndDirection(RHICmdList, InvBufferSize, FVector2f(1.f, 0.f));

					RendererModule.DrawRectangle(
						RHICmdList,
						0.f, 0.f,
						(float)DownsampleSize.X, (float)DownsampleSize.Y,
						0, 0,
						(float)DownsampleSize.X, (float)DownsampleSize.Y,
						FIntPoint(DestTextureWidth, DestTextureHeight),
						FIntPoint(DestTextureWidth, DestTextureHeight),
						VertexShader,
						EDRF_Default);
				}
				else
				{
					const FVector2f InvSrcTextureSize(1.f / SrcTextureWidth, 1.f / SrcTextureHeight);

					const FVector2f UVStart = FVector2f(DestRect.Left, DestRect.Top) * InvSrcTextureSize;
					const FVector2f UVEnd = FVector2f(DestRect.Right, DestRect.Bottom) * InvSrcTextureSize;
					const FVector2f SizeUV = UVEnd - UVStart;

					PixelShader->SetUVBounds(RHICmdList, FVector4f(UVStart, UVEnd));
					PixelShader->SetBufferSizeAndDirection(RHICmdList, InvSrcTextureSize, FVector2f(1.f, 0.f));

					RendererModule.DrawRectangle(
						RHICmdList,
						0.f, 0.f,
						(float)RequiredSize.X, (float)RequiredSize.Y,
						UVStart.X, UVStart.Y,
						SizeUV.X, SizeUV.Y,
						FIntPoint(DestTextureWidth, DestTextureHeight),
						FIntPoint(1, 1),
						VertexShader,
						EDRF_Default);
				}
			}
			RHICmdList.EndRenderPass();
		}
		else
		{
			FTexture2DRHIRef SourceTexture = IntermediateTargets->GetRenderTarget(1);
			FTexture2DRHIRef DestTexture = IntermediateTargets->GetRenderTarget(0);

			RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
			RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("SlateBlurRect"));
			{
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = VertexDecl;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				PixelShader->SetWeightsAndOffsets(RHICmdList, WeightsAndOffsets, SampleCount);
				PixelShader->SetUVBounds(RHICmdList, FVector4f(FVector2f::ZeroVector, FVector2f((float)DownsampleSize.X / DestTextureWidth, (float)DownsampleSize.Y / DestTextureHeight) - HalfTexelOffset));
				PixelShader->SetTexture(RHICmdList, SourceTexture, BilinearClamp);
				PixelShader->SetBufferSizeAndDirection(RHICmdList, InvBufferSize, FVector2f(0.f, 1.f));

				RendererModule.DrawRectangle(
					RHICmdList,
					0.f, 0.f,
					(float)DownsampleSize.X, (float)DownsampleSize.Y,
					0.f, 0.f,
					(float)DownsampleSize.X, (float)DownsampleSize.Y,
					FIntPoint(DestTextureWidth, DestTextureHeight),
					FIntPoint(DestTextureWidth, DestTextureHeight),
					VertexShader,
					EDRF_Default);
			}
			RHICmdList.EndRenderPass();
		}	
	}

#endif

	UpsampleRect(RHICmdList, RendererModule, RectParams, DownsampleSize, BilinearClamp);
}

void FSlatePostProcessor::ColorDeficiency(FRHICommandListImmediate& RHICmdList, IRendererModule& RendererModule, const FPostProcessRectParams& RectParams)
{
	SCOPE_CYCLE_COUNTER(STAT_SlateColorDeficiencyRTTime);

	FIntPoint DestRectSize = RectParams.DestRect.GetSize().IntPoint();
	FIntPoint RequiredSize = DestRectSize;

	IntermediateTargets->Update(RequiredSize, RectParams.SourceTexture);

#if 1
	FSamplerStateRHIRef PointClamp = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	check(ShaderMap);

	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<FSlatePostProcessColorDeficiencyPS> PixelShader(ShaderMap);

	const int32 SrcTextureWidth = RectParams.SourceTextureSize.X;
	const int32 SrcTextureHeight = RectParams.SourceTextureSize.Y;

	const int32 DestTextureWidth = IntermediateTargets->GetWidth();
	const int32 DestTextureHeight = IntermediateTargets->GetHeight();

	const FSlateRect& SourceRect = RectParams.SourceRect;
	const FSlateRect& DestRect = RectParams.DestRect;

	FVertexDeclarationRHIRef VertexDecl = GFilterVertexDeclaration.VertexDeclarationRHI;
	check(IsValidRef(VertexDecl));

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
	RHICmdList.SetViewport(0.f, 0.f, 0.f, (float)DestTextureWidth, (float)DestTextureHeight, 0.0f);

	// 
	{
		FTexture2DRHIRef SourceTexture = RectParams.SourceTexture;
		FTexture2DRHIRef DestTexture = IntermediateTargets->GetRenderTarget(0);

		RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
		RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

		FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("ColorDeficiency"));
		{
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = VertexDecl;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			PixelShader->SetColorRules(RHICmdList, GSlateColorDeficiencyCorrection, GSlateColorDeficiencyType, GSlateColorDeficiencySeverity);
			PixelShader->SetShowCorrectionWithDeficiency(RHICmdList, GSlateShowColorDeficiencyCorrectionWithDeficiency);
			PixelShader->SetTexture(RHICmdList, SourceTexture, PointClamp);

			RendererModule.DrawRectangle(
				RHICmdList,
				0.f, 0,
				(float)RequiredSize.X, (float)RequiredSize.Y,
				0.f, 0.f,
				1.f, 1.f,
				FIntPoint(DestTextureWidth, DestTextureHeight),
				FIntPoint(1, 1),
				VertexShader,
				EDRF_Default);
		}
		RHICmdList.EndRenderPass();
	}

	const FIntPoint DownsampleSize = RequiredSize;
	UpsampleRect(RHICmdList, RendererModule, RectParams, DownsampleSize, PointClamp);

#endif
}

void FSlatePostProcessor::ReleaseRenderTargets()
{
	check(IsInGameThread());
	// Only release the resource not delete it.  Deleting it could cause issues on any RHI thread
	BeginReleaseResource(IntermediateTargets);
}

void FSlatePostProcessor::DownsampleRect(FRHICommandListImmediate& RHICmdList, IRendererModule& RendererModule, const FPostProcessRectParams& Params, const FIntPoint& DownsampleSize)
{
	SCOPED_DRAW_EVENT(RHICmdList, SlatePostProcessDownsample);

	// Source is the viewport.  This is the width and height of the viewport backbuffer
	const int32 SrcTextureWidth = Params.SourceTextureSize.X;
	const int32 SrcTextureHeight = Params.SourceTextureSize.Y;

	// Dest is the destination quad for the downsample
	const int32 DestTextureWidth = IntermediateTargets->GetWidth();
	const int32 DestTextureHeight = IntermediateTargets->GetHeight();

	// Rect of the viewport
	const FSlateRect& SourceRect = Params.SourceRect;

	// Rect of the final destination post process effect (not downsample rect).  This is the area we sample from
	const FSlateRect& DestRect = Params.DestRect;

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);

	FSamplerStateRHIRef BilinearClamp = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FTexture2DRHIRef DestTexture = IntermediateTargets->GetRenderTarget(0);

	// Downsample and store in intermediate texture
	{
		TShaderMapRef<FSlatePostProcessDownsamplePS> PixelShader(ShaderMap);

		RHICmdList.Transition(FRHITransitionInfo(Params.SourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
		RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

		const FVector2f InvSrcTextureSize(1.f/SrcTextureWidth, 1.f/SrcTextureHeight);

		const FVector2f UVStart = FVector2f(DestRect.Left, DestRect.Top) * InvSrcTextureSize;
		const FVector2f UVEnd = FVector2f(DestRect.Right, DestRect.Bottom) * InvSrcTextureSize;
		const FVector2f SizeUV = UVEnd - UVStart;
		
		RHICmdList.SetViewport(0.f, 0.f, 0.f, (float)DestTextureWidth, (float)DestTextureHeight, 0.0f);
		RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

		FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("DownsampleRect"));
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			PixelShader->SetShaderParams(RHICmdList, FShaderParams::MakePixelShaderParams(FVector4f(InvSrcTextureSize.X, InvSrcTextureSize.Y, 0, 0)));
			PixelShader->SetUVBounds(RHICmdList, FVector4f(UVStart, UVEnd));
			PixelShader->SetTexture(RHICmdList, Params.SourceTexture, BilinearClamp);

			RendererModule.DrawRectangle(
				RHICmdList,
				0.f, 0.f,
				(float)DownsampleSize.X, (float)DownsampleSize.Y,
				UVStart.X, UVStart.Y,
				SizeUV.X, SizeUV.Y,
				FIntPoint(DestTextureWidth, DestTextureHeight),
				FIntPoint(1, 1),
				VertexShader,
				EDRF_Default);
		}
		RHICmdList.EndRenderPass();
	}
	
	// Testing only
#if 0
	UpsampleRect(RHICmdList, RendererModule, Params, DownsampleSize);
#endif
}

void FSlatePostProcessor::UpsampleRect(FRHICommandListImmediate& RHICmdList, IRendererModule& RendererModule, const FPostProcessRectParams& Params, const FIntPoint& DownsampleSize, FSamplerStateRHIRef& Sampler)
{
	SCOPED_DRAW_EVENT(RHICmdList, SlatePostProcessUpsample);

	const FVector4f Zero(0.f, 0.f, 0.f, 0.f);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.BlendState = Params.CornerRadius == Zero ? TStaticBlendState<>::GetRHI() : TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	// Original source texture is now the destination texture
	FTexture2DRHIRef DestTexture = Params.SourceTexture;
	const int32 DestTextureWidth = Params.SourceTextureSize.X;
	const int32 DestTextureHeight = Params.SourceTextureSize.Y;

	const int32 DownsampledWidth = DownsampleSize.X;
	const int32 DownsampledHeight = DownsampleSize.Y;

	// Source texture is the texture that was originally downsampled
	FTexture2DRHIRef SrcTexture = IntermediateTargets->GetRenderTarget(0);
	const int32 SrcTextureWidth = IntermediateTargets->GetWidth();
	const int32 SrcTextureHeight = IntermediateTargets->GetHeight();

	const FSlateRect& SourceRect = Params.SourceRect;
	const FSlateRect& DestRect = Params.DestRect;

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);

	RHICmdList.SetViewport(0.f, 0.f, 0.f, (float)DestTextureWidth, (float)DestTextureHeight, 0.0f);

	// Perform Writable transitions first

	RHICmdList.Transition(FRHITransitionInfo(SrcTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
	RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

	FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);

	bool bHasMRT = false;

	FRHITexture* UITargetTexture = Params.UITarget.IsValid() ? Params.UITarget->GetRHI() : nullptr;

	if (UITargetTexture != nullptr && DestTexture != UITargetTexture)
	{
		RPInfo.ColorRenderTargets[1].RenderTarget = UITargetTexture;
		RPInfo.ColorRenderTargets[1].ArraySlice = -1;
		RPInfo.ColorRenderTargets[1].Action = ERenderTargetActions::Load_Store;
		if (!RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform))
		{
			RPInfo.ColorRenderTargets[2].RenderTarget = Params.UITargetMask->GetRHI();
			RPInfo.ColorRenderTargets[2].ArraySlice = -1;
			RPInfo.ColorRenderTargets[2].Action = ERenderTargetActions::Load_Store;
		}
		bHasMRT = true;
	}

	RHICmdList.BeginRenderPass(RPInfo, TEXT("UpsampleRect"));
	{
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		if (Params.RestoreStateFunc)
		{
			// This can potentially end and restart a renderpass.
			// #todo refactor so that we only start one renderpass here. Right now RestoreStateFunc may call UpdateScissorRect which requires an open renderpass.
			Params.RestoreStateFunc(RHICmdList, GraphicsPSOInit);
		}

		TShaderRef<FSlateElementPS> PixelShader;
		if (bHasMRT)
		{
			PixelShader = TShaderMapRef<FSlatePostProcessUpsamplePS<true> >(ShaderMap);
		}
		else
		{
			PixelShader = TShaderMapRef<FSlatePostProcessUpsamplePS<false> >(ShaderMap);
		}

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, Params.StencilRef);

		const FVector2f SizeUV(
			DownsampledWidth == SrcTextureWidth ? 1.0f : (DownsampledWidth / (float)SrcTextureWidth) - (1.0f / (float)SrcTextureWidth),
			DownsampledHeight == SrcTextureHeight ? 1.0f : (DownsampledHeight / (float)SrcTextureHeight) - (1.0f / (float)SrcTextureHeight)
			);

		const FVector2f Size(DestRect.Right - DestRect.Left, DestRect.Bottom - DestRect.Top);
		FShaderParams ShaderParams = FShaderParams::MakePixelShaderParams(FVector4f(Size, SizeUV), Params.CornerRadius);


		PixelShader->SetShaderParams(RHICmdList, ShaderParams);
		PixelShader->SetTexture(RHICmdList, SrcTexture, Sampler);


		RendererModule.DrawRectangle(RHICmdList,
			DestRect.Left, DestRect.Top,
			Size.X, Size.Y,
			0, 0,
			SizeUV.X, SizeUV.Y,
			Params.SourceTextureSize,
			FIntPoint(1, 1),
			VertexShader,
			EDRF_Default);
	}
	RHICmdList.EndRenderPass();

}
#define BILINEAR_FILTER_METHOD 1

#if !BILINEAR_FILTER_METHOD

static int32 ComputeWeights(int32 KernelSize, float Sigma, TArray<FVector4f>& OutWeightsAndOffsets)
{
	OutWeightsAndOffsets.AddUninitialized(KernelSize / 2 + 1);

	int32 SampleIndex = 0;
	for (int32 X = 0; X < KernelSize; X += 2)
	{
		float Dist = X;
		FVector4f WeightAndOffset;
		WeightAndOffset.X = (1.0f / FMath::Sqrt(2 * PI*Sigma*Sigma))*FMath::Exp(-(Dist*Dist) / (2 * Sigma*Sigma));
		WeightAndOffset.Y = Dist;

		Dist = X + 1;
		WeightAndOffset.Z = (1.0f / FMath::Sqrt(2 * PI*Sigma*Sigma))*FMath::Exp(-(Dist*Dist) / (2 * Sigma*Sigma));
		WeightAndOffset.W = Dist;

		OutWeightsAndOffsets[SampleIndex] = WeightAndOffset;

		++SampleIndex;
	}

	return KernelSize;
};

#else

static float GetWeight(float Dist, float Strength)
{
	// from https://en.wikipedia.org/wiki/Gaussian_blur
	float Strength2 = Strength*Strength;
	return (1.0f / FMath::Sqrt(2 * PI*Strength2))*FMath::Exp(-(Dist*Dist) / (2 * Strength2));
}

static FVector2f GetWeightAndOffset(float Dist, float Sigma)
{
	float Offset1 = Dist;
	float Weight1 = GetWeight(Offset1, Sigma);

	float Offset2 = Dist + 1;
	float Weight2 = GetWeight(Offset2, Sigma);

	float TotalWeight = Weight1 + Weight2;

	float Offset = 0;
	if (TotalWeight > 0)
	{
		Offset = (Weight1*Offset1 + Weight2*Offset2) / TotalWeight;
	}


	return FVector2f(TotalWeight, Offset);
}

static int32 ComputeWeights(int32 KernelSize, float Sigma, TArray<FVector4f>& OutWeightsAndOffsets)
{
	int32 NumSamples = FMath::DivideAndRoundUp(KernelSize, 2);

	// We need half of the sample count array because we're packing two samples into one float4

	OutWeightsAndOffsets.AddUninitialized(NumSamples%2 == 0 ? NumSamples / 2 : NumSamples/2+1);

	OutWeightsAndOffsets[0] = FVector4f(FVector2f(GetWeight(0,Sigma), 0), GetWeightAndOffset(1, Sigma) );
	int32 SampleIndex = 1;
	for (int32 X = 3; X < KernelSize; X += 4)
	{
		OutWeightsAndOffsets[SampleIndex] = FVector4f(GetWeightAndOffset((float)X, Sigma), GetWeightAndOffset((float)(X + 2), Sigma));

		++SampleIndex;
	}

	return NumSamples;
};

#endif

int32 FSlatePostProcessor::ComputeBlurWeights(int32 KernelSize, float StdDev, TArray<FVector4f>& OutWeightsAndOffsets)
{
	return ComputeWeights(KernelSize, StdDev, OutWeightsAndOffsets);
}

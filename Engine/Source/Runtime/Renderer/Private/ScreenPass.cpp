// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ScreenPass.h"
#include "EngineGlobals.h"
#include "ScenePrivate.h"
#include "RendererModule.h"
#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"
#include "RenderGraphUtils.h"

extern bool ShouldDoComputePostProcessing(const FViewInfo& View);

IMPLEMENT_GLOBAL_SHADER(FScreenPassVS, "/Engine/Private/ScreenPass.usf", "ScreenPassVS", SF_Vertex);

const FTextureRHIRef& GetMiniFontTexture()
{
	if (GEngine->MiniFontTexture)
	{
		return GEngine->MiniFontTexture->Resource->TextureRHI;
	}
	else
	{
		return GSystemTextures.WhiteDummy->GetRenderTargetItem().TargetableTexture;
	}
}

bool IsHMDHiddenAreaMaskActive()
{
	// Query if we have a custom HMD post process mesh to use
	static const auto* const HiddenAreaMaskCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.HiddenAreaMask"));

	return
		HiddenAreaMaskCVar != nullptr &&
		HiddenAreaMaskCVar->GetValueOnRenderThread() == 1 &&
		GEngine &&
		GEngine->XRSystem.IsValid() && GEngine->XRSystem->GetHMDDevice() &&
		GEngine->XRSystem->GetHMDDevice()->HasVisibleAreaMesh();
}

FScreenPassTextureViewport FScreenPassTextureViewport::CreateDownscaled(const FScreenPassTextureViewport& Other, uint32 ScaleFactor)
{
	const auto GetDownscaledSize = [](FIntPoint Size, uint32 InScaleFactor)
	{
		Size = FIntPoint::DivideAndRoundUp(Size, InScaleFactor);
		Size.X = FMath::Max(1, Size.X);
		Size.Y = FMath::Max(1, Size.Y);
		return Size;
	};

	FScreenPassTextureViewport Viewport;
	Viewport.Rect.Min = Other.Rect.Min / ScaleFactor;
	Viewport.Rect.Max = GetDownscaledSize(Other.Rect.Max, ScaleFactor);
	Viewport.Extent = GetDownscaledSize(Other.Extent, ScaleFactor);
	return Viewport;
}

bool FScreenPassTextureViewport::operator==(const FScreenPassTextureViewport& Other) const
{
	return Rect == Other.Rect && Extent == Other.Extent;
}

bool FScreenPassTextureViewport::operator!=(const FScreenPassTextureViewport& Other) const
{
	return Rect != Other.Rect || Extent != Other.Extent;
}

bool FScreenPassTextureViewport::IsEmpty() const
{
	return Rect.IsEmpty() || Extent == FIntPoint::ZeroValue;
}

FScreenPassTextureViewportParameters GetScreenPassTextureViewportParameters(const FScreenPassTextureViewport& InViewport)
{
	const FVector2D Extent(InViewport.Extent);
	const FVector2D ViewportMin(InViewport.Rect.Min.X, InViewport.Rect.Min.Y);
	const FVector2D ViewportMax(InViewport.Rect.Max.X, InViewport.Rect.Max.Y);
	const FVector2D ViewportSize = ViewportMax - ViewportMin;

	FScreenPassTextureViewportParameters Parameters;

	if (!InViewport.IsEmpty())
	{
		Parameters.Extent = Extent;
		Parameters.ExtentInverse = FVector2D(1.0f / Extent.X, 1.0f / Extent.Y);

		Parameters.ScreenPosToViewportScale = FVector2D(0.5f, -0.5f) * ViewportSize;
		Parameters.ScreenPosToViewportBias = (0.5f * ViewportSize) + ViewportMin;

		Parameters.ViewportMin = InViewport.Rect.Min;
		Parameters.ViewportMax = InViewport.Rect.Max;

		Parameters.ViewportSize = ViewportSize;
		Parameters.ViewportSizeInverse = FVector2D(1.0f / Parameters.ViewportSize.X, 1.0f / Parameters.ViewportSize.Y);

		Parameters.UVViewportMin = ViewportMin * Parameters.ExtentInverse;
		Parameters.UVViewportMax = ViewportMax * Parameters.ExtentInverse;

		Parameters.UVViewportSize = Parameters.UVViewportMax - Parameters.UVViewportMin;
		Parameters.UVViewportSizeInverse = FVector2D(1.0f / Parameters.UVViewportSize.X, 1.0f / Parameters.UVViewportSize.Y);

		Parameters.UVViewportBilinearMin = Parameters.UVViewportMin + 0.5f * Parameters.ExtentInverse;
		Parameters.UVViewportBilinearMax = Parameters.UVViewportMax - 0.5f * Parameters.ExtentInverse;
	}

	return Parameters;
}

FScreenPassTextureViewportTransform GetScreenPassTextureViewportTransform(
	FVector2D SourceOffset,
	FVector2D SourceExtent,
	FVector2D DestinationOffset,
	FVector2D DestinationExtent)
{
	FScreenPassTextureViewportTransform Transform;
	Transform.Scale = DestinationExtent / SourceExtent;
	Transform.Bias = DestinationOffset - Transform.Scale * SourceOffset;
	return Transform;
}

FScreenPassTextureViewportTransform GetScreenPassTextureViewportTransform(
	const FScreenPassTextureViewportParameters& Source,
	const FScreenPassTextureViewportParameters& Destination)
{
	const FVector2D SourceUVOffset = Source.UVViewportMin;
	const FVector2D SourceUVExtent = Source.UVViewportSize;
	const FVector2D DestinationUVOffset = Destination.UVViewportMin;
	const FVector2D DestinationUVExtent = Destination.UVViewportSize;

	return GetScreenPassTextureViewportTransform(SourceUVOffset, SourceUVExtent, DestinationUVOffset, DestinationUVExtent);
}

FScreenPassViewInfo::FScreenPassViewInfo(const FViewInfo& InView)
	: View(InView)
	, ScreenPassVS(View.ShaderMap)
	, StereoPass(View.StereoPass)
	, bHasHMDMask(IsHMDHiddenAreaMaskActive())
	, bUseComputePasses(ShouldDoComputePostProcessing(InView))
{}

ERenderTargetLoadAction FScreenPassViewInfo::GetOverwriteLoadAction() const
{
	return bHasHMDMask ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ENoAction;
}

void SetScreenPassPipelineState(FRHICommandListImmediate& RHICmdList, const FScreenPassDrawInfo& ScreenPassDraw)
{
	FRHIPixelShader* PixelShaderRHI = GETSAFERHISHADER_PIXEL(ScreenPassDraw.PixelShader);
	FRHIVertexShader* VertexShaderRHI = GETSAFERHISHADER_VERTEX(ScreenPassDraw.VertexShader);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = ScreenPassDraw.BlendState;
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = ScreenPassDraw.DepthStencilState;
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShaderRHI;
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShaderRHI;
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
}

void AddDrawTexturePass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	FRDGTextureRef InputTexture,
	FRDGTextureRef OutputTexture,
	FIntPoint InputPosition,
	FIntPoint OutputPosition,
	FIntPoint Size)
{
	const FRDGTextureDesc& InputDesc = InputTexture->Desc;
	const FRDGTextureDesc& OutputDesc = OutputTexture->Desc;

	// Use a hardware copy if formats match.
	if (InputDesc.Format == OutputDesc.Format)
	{
		return AddCopyTexturePass(GraphBuilder, InputTexture, OutputTexture, InputPosition, OutputPosition, Size);
	}

	if (Size == FIntPoint::ZeroValue)
	{
		// Copy entire input texture to output texture.
		Size = InputTexture->Desc.Extent;
	}

	// Don't prime color data if the whole texture is being overwritten.
	const ERenderTargetLoadAction LoadAction = (OutputPosition == FIntPoint::ZeroValue && Size == OutputDesc.Extent)
		? ERenderTargetLoadAction::ENoAction
		: ERenderTargetLoadAction::ELoad;

	const FScreenPassTextureViewport InputViewport(FIntRect(InputPosition, InputPosition + Size), InputTexture);
	const FScreenPassTextureViewport OutputViewport(FIntRect(OutputPosition, OutputPosition + Size), OutputTexture);

	TShaderMapRef<FCopyRectPS> PixelShader(ScreenPassView.View.ShaderMap);

	FCopyRectPS::FParameters* Parameters = GraphBuilder.AllocParameters<FCopyRectPS::FParameters>();
	Parameters->InputTexture = InputTexture;
	Parameters->InputSampler = TStaticSamplerState<>::GetRHI();
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, LoadAction);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("DrawTexturePass"),
		ScreenPassView,
		OutputViewport,
		InputViewport,
		*PixelShader,
		Parameters);
}
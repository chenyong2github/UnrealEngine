// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

inline FIntPoint GetDownscaledExtent(FIntPoint Extent, FIntPoint Divisor)
{
	Extent = FIntPoint::DivideAndRoundUp(Extent, Divisor);
	Extent.X = FMath::Max(1, Extent.X);
	Extent.Y = FMath::Max(1, Extent.Y);
	return Extent;
}

inline FIntPoint GetScaledExtent(FIntPoint Extent, FVector2D Multiplier)
{
	Extent.X *= Multiplier.X;
	Extent.Y *= Multiplier.Y;
	Extent.X = FMath::Max(1, Extent.X);
	Extent.Y = FMath::Max(1, Extent.Y);
	return Extent;
}

inline FIntPoint GetScaledExtent(FIntPoint Extent, float Multiplier)
{
	return GetScaledExtent(Extent, FVector2D(Multiplier));
}

inline FIntRect GetDownscaledRect(FIntRect Rect, FIntPoint Divisor)
{
	Rect.Min /= Divisor;
	Rect.Max = GetDownscaledExtent(Rect.Max, Divisor);
	return Rect;
}

inline FIntRect GetScaledRect(FIntRect Rect, FVector2D Multiplier)
{
	Rect.Min.X *= Multiplier.X;
	Rect.Min.Y *= Multiplier.Y;
	Rect.Max = GetScaledExtent(Rect.Max, Multiplier);
	return Rect;
}

FORCEINLINE FIntRect GetScaledRect(FIntRect Rect, float Multiplier)
{
	return GetScaledRect(Rect, FVector2D(Multiplier));
}

inline FScreenPassTextureViewport GetDownscaledViewport(FScreenPassTextureViewport Viewport, FIntPoint Divisor)
{
	Viewport.Rect = GetDownscaledRect(Viewport.Rect, Divisor);
	Viewport.Extent = GetDownscaledExtent(Viewport.Extent, Divisor);
	return Viewport;
}

inline FScreenPassTextureViewport GetScaledViewport(FScreenPassTextureViewport Viewport, FVector2D Multiplier)
{
	Viewport.Rect = GetScaledRect(Viewport.Rect, Multiplier);
	Viewport.Extent = GetScaledExtent(Viewport.Extent, Multiplier);
	return Viewport;
}

inline FIntRect GetRectFromExtent(FIntPoint Extent)
{
	return FIntRect(FIntPoint::ZeroValue, Extent);
}

inline FScreenPassRenderTarget FScreenPassRenderTarget::CreateFromInput(
	FRDGBuilder& GraphBuilder,
	FScreenPassTexture Input,
	ERenderTargetLoadAction OutputLoadAction,
	const TCHAR* OutputName)
{
	check(Input.IsValid());

	FRDGTextureDesc OutputDesc = Input.Texture->Desc;
	OutputDesc.Reset();

	return FScreenPassRenderTarget(GraphBuilder.CreateTexture(OutputDesc, OutputName), Input.ViewRect, OutputLoadAction);
}

inline FScreenPassRenderTarget FScreenPassRenderTarget::CreateViewFamilyOutput(FRDGTextureRef ViewFamilyTexture, const FViewInfo& View)
{
	return FScreenPassRenderTarget(
		ViewFamilyTexture,
		// Raw output mode uses the original view rect. Otherwise the final unscaled rect is used.
		View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::RawOutput ? View.ViewRect : View.UnscaledViewRect,
		// First view clears the view family texture; all remaining views load.
		(!View.Family->bAdditionalViewFamily && View.IsFirstInFamily() )? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
}

inline FScreenPassTexture::FScreenPassTexture(FRDGTextureRef InTexture)
	: Texture(InTexture)
{
	if (InTexture)
	{
		ViewRect.Max = InTexture->Desc.Extent;
	}
}

inline bool FScreenPassTexture::IsValid() const
{
	return Texture != nullptr && !ViewRect.IsEmpty();
}

inline bool FScreenPassTexture::operator==(FScreenPassTexture Other) const
{
	return Texture == Other.Texture && ViewRect == Other.ViewRect;
}

inline bool FScreenPassTexture::operator!=(FScreenPassTexture Other) const
{
	return !(*this == Other);
}

inline bool FScreenPassRenderTarget::operator==(FScreenPassRenderTarget Other) const
{
	return FScreenPassTexture::operator==(Other) && LoadAction == Other.LoadAction;
}

inline bool FScreenPassRenderTarget::operator!=(FScreenPassRenderTarget Other) const
{
	return !(*this == Other);
}

inline FRenderTargetBinding FScreenPassRenderTarget::GetRenderTargetBinding() const
{
	return FRenderTargetBinding(Texture, LoadAction);
}

inline FScreenPassTextureViewport::FScreenPassTextureViewport(FScreenPassTexture InTexture)
{
	check(InTexture.IsValid());
	Extent = InTexture.Texture->Desc.Extent;
	Rect = InTexture.ViewRect;
}

inline bool FScreenPassTextureViewport::operator==(const FScreenPassTextureViewport& Other) const
{
	return Extent == Other.Extent && Rect == Other.Rect;
}

inline bool FScreenPassTextureViewport::operator!=(const FScreenPassTextureViewport& Other) const
{
	return Extent != Other.Extent || Rect != Other.Rect;
}

inline bool FScreenPassTextureViewport::IsEmpty() const
{
	return Extent == FIntPoint::ZeroValue || Rect.IsEmpty();
}

inline bool FScreenPassTextureViewport::IsFullscreen() const
{
	return Rect.Min == FIntPoint::ZeroValue && Rect.Max == Extent;
}

inline FVector2D FScreenPassTextureViewport::GetRectToExtentRatio() const
{
	return FVector2D((float)Rect.Width() / Extent.X, (float)Rect.Height() / Extent.Y);
}

inline FScreenPassTextureViewportTransform GetScreenPassTextureViewportTransform(
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

inline FScreenPassTextureViewportTransform GetScreenPassTextureViewportTransform(
	const FScreenPassTextureViewportParameters& Source,
	const FScreenPassTextureViewportParameters& Destination)
{
	const FVector2D SourceUVOffset = Source.UVViewportMin;
	const FVector2D SourceUVExtent = Source.UVViewportSize;
	const FVector2D DestinationUVOffset = Destination.UVViewportMin;
	const FVector2D DestinationUVExtent = Destination.UVViewportSize;

	return GetScreenPassTextureViewportTransform(SourceUVOffset, SourceUVExtent, DestinationUVOffset, DestinationUVExtent);
}

inline FScreenPassTextureInput GetScreenPassTextureInput(FScreenPassTexture TexturePair, FRHISamplerState* Sampler)
{
	FScreenPassTextureInput Input;
	Input.Viewport = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(TexturePair));
	Input.Texture = TexturePair.Texture;
	Input.Sampler = Sampler;
	return Input;
}
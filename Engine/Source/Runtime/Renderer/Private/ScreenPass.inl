// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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

inline FScreenPassTextureViewport FScreenPassTextureViewport::CreateDownscaled(const FScreenPassTextureViewport& Other, uint32 DownscaleFactor)
{
	return CreateDownscaled(Other, FIntPoint(DownscaleFactor, DownscaleFactor));
}

inline FScreenPassTextureViewport FScreenPassTextureViewport::CreateDownscaled(const FScreenPassTextureViewport& Other, FIntPoint ScaleFactor)
{
	const auto GetDownscaledSize = [](FIntPoint Size, FIntPoint InScaleFactor)
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
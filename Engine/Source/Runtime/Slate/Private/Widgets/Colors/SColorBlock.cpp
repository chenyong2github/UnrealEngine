// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Colors/SColorBlock.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"


/**
 * Construct this widget
 *
 * @param	InArgs	The declaration data for this widget
 */
void SColorBlock::Construct(const FArguments& InArgs)
{
	Color = InArgs._Color;
	AlphaBackgroundBrush = InArgs._AlphaBackgroundBrush;
	SolidBackgroundBrush = InArgs._SolidBackgroundBrush;
	GradientCornerRadius = InArgs._CornerRadius;
	ColorIsHSV = InArgs._ColorIsHSV;
	AlphaDisplayMode = InArgs._AlphaDisplayMode;
	ShowBackgroundForAlpha = InArgs._ShowBackgroundForAlpha;
	MouseButtonDownHandler = InArgs._OnMouseButtonDown;
	bUseSRGB = InArgs._UseSRGB;
	ColorBlockSize = InArgs._Size;
}

int32 SColorBlock::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const ESlateDrawEffect DrawEffects = ESlateDrawEffect::None;

	EColorBlockAlphaDisplayMode DisplayMode = AlphaDisplayMode.Get();
	bool bIgnoreAlpha = DisplayMode == EColorBlockAlphaDisplayMode::Ignore;
	FLinearColor InColor = Color.Get();
	if (ColorIsHSV.Get())
	{
		InColor = InColor.HSVToLinearRGB();
	}

	if (ShowBackgroundForAlpha.Get() && InColor.A < 1.0f && !bIgnoreAlpha)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), AlphaBackgroundBrush.Get(), DrawEffects);
	}

	TArray<FSlateGradientStop> GradientStops;

	switch (DisplayMode)
	{
	default:
	case EColorBlockAlphaDisplayMode::Combined:
		MakeSection(GradientStops, FVector2D::ZeroVector, AllottedGeometry.GetLocalSize(), InColor, InWidgetStyle, false);
		break;
	case EColorBlockAlphaDisplayMode::Separate:
		MakeSection(GradientStops, FVector2D::ZeroVector, AllottedGeometry.GetLocalSize() * 0.5f, InColor, InWidgetStyle, false);
		MakeSection(GradientStops, AllottedGeometry.GetLocalSize() * 0.5f, AllottedGeometry.GetLocalSize(), InColor, InWidgetStyle, true);
		break;
	case EColorBlockAlphaDisplayMode::Ignore:
		MakeSection(GradientStops, FVector2D::ZeroVector, AllottedGeometry.GetLocalSize(), InColor, InWidgetStyle, true);
		break;
	}

	FSlateDrawElement::MakeGradient(
		OutDrawElements,
		LayerId+1,
		AllottedGeometry.ToPaintGeometry(),
		MoveTemp(GradientStops),
		(AllottedGeometry.GetLocalSize().X > AllottedGeometry.GetLocalSize().Y) ? Orient_Vertical : Orient_Horizontal,
		DrawEffects,
		GradientCornerRadius.Get(0.0f)
	);


	return LayerId + 1;
}

FReply SColorBlock::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseButtonDownHandler.IsBound())
	{
		// If a handler is assigned, call it.
		return MouseButtonDownHandler.Execute(MyGeometry, MouseEvent);
	}
	else
	{
		// otherwise the event is unhandled.
		return FReply::Unhandled();
	}
}

FVector2D SColorBlock::ComputeDesiredSize(float) const
{
	return ColorBlockSize.Get();
}

void SColorBlock::MakeSection(TArray<FSlateGradientStop>& OutGradientStops, FVector2D StartPt, FVector2D EndPt, FLinearColor InColor, const FWidgetStyle& InWidgetStyle, bool bIgnoreAlpha) const
{
	// determine if it is HDR
	const float MaxRGB = FMath::Max3(InColor.R, InColor.G, InColor.B);
	if (MaxRGB > 1.f)
	{
		const float Alpha = bIgnoreAlpha ? 1.0f : InColor.A;
		FLinearColor NormalizedLinearColor = InColor / MaxRGB;
		NormalizedLinearColor.A = Alpha;
		const FLinearColor DrawNormalizedColor = InWidgetStyle.GetColorAndOpacityTint() * NormalizedLinearColor.ToFColor(bUseSRGB.Get());

		FLinearColor ClampedLinearColor = InColor;
		ClampedLinearColor.A = Alpha * MaxRGB;
		const FLinearColor DrawClampedColor = InWidgetStyle.GetColorAndOpacityTint() * ClampedLinearColor.ToFColor(bUseSRGB.Get());

		OutGradientStops.Add(FSlateGradientStop(StartPt, DrawNormalizedColor));
		OutGradientStops.Add(FSlateGradientStop((StartPt + EndPt) * 0.5f, DrawClampedColor));
		OutGradientStops.Add(FSlateGradientStop(EndPt, DrawNormalizedColor));
	}
	else
	{
		FColor DrawColor = InColor.ToFColor(bUseSRGB.Get());
		if (bIgnoreAlpha)
		{
			DrawColor.A = 255;
		}
		OutGradientStops.Add(FSlateGradientStop(StartPt, InWidgetStyle.GetColorAndOpacityTint() * DrawColor));
		OutGradientStops.Add(FSlateGradientStop(EndPt, InWidgetStyle.GetColorAndOpacityTint() * DrawColor));
	}
}

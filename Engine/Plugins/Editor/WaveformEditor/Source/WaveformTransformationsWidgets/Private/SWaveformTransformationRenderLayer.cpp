// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWaveformTransformationRenderLayer.h"

#include "Styling/AppStyle.h"

void SWaveformTransformationRenderLayer::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
}

void SWaveformTransformationRenderLayer::Construct(const FArguments& InArgs)
{
}

int32 SWaveformTransformationRenderLayer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{

	auto DisplayString = FString::Printf(TEXT("basic transform layer %d"), LayerId);

	FSlateDrawElement::MakeText(
		OutDrawElements,
		++LayerId,
		AllottedGeometry.ToPaintGeometry(),
		DisplayString,
		FAppStyle::GetFontStyle("Regular"),
		ESlateDrawEffect::None,
		FLinearColor::Green
	);

	return LayerId;
}

FVector2D SWaveformTransformationRenderLayer::ComputeDesiredSize(float) const
{
	return FVector2D(1280, 720);
}

void SWaveformTransformationRenderLayer::SetTransformationWaveInfo(FWaveformTransformationRenderLayerInfo InWaveInfo)
{
	TransformationWaveInfo = InWaveInfo;
}

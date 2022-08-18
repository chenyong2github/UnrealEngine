// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

struct WAVEFORMTRANSFORMATIONSWIDGETS_API FWaveformTransformationRenderLayerInfo
{
	float SampleRate = 0.f;
	int32 NumChannels = 0;
	uint32 StartFrameOffset = 0;
	uint32 NumEditedSamples = 0;
};

class WAVEFORMTRANSFORMATIONSWIDGETS_API SWaveformTransformationRenderLayer : public SCompoundWidget
{
	SLATE_DECLARE_WIDGET(SWaveformTransformationRenderLayer, SCompoundWidget)

public:
	SLATE_BEGIN_ARGS(SWaveformTransformationRenderLayer) {}
		SLATE_DEFAULT_SLOT(FArguments, InArgs)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	FVector2D ComputeDesiredSize(float) const override;

	void SetTransformationWaveInfo(FWaveformTransformationRenderLayerInfo InWaveInfo);


protected:
	FWaveformTransformationRenderLayerInfo TransformationWaveInfo;


};
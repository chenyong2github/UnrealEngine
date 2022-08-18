// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class SOverlay;

class WAVEFORMEDITORWIDGETS_API SWaveformTransformationsOverlay : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWaveformTransformationsOverlay) {}
		SLATE_DEFAULT_SLOT(FArguments, InArgs)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TArrayView<TSharedPtr<SWidget>> InTransformationLayers);
	void OnLayerChainUpdate(TSharedPtr<SWidget>* FirstLayerPtr, const int32 NLayers);

private:
	void CreateLayout();
	
	TSharedPtr<SOverlay> MainOverlayPtr;
	TArrayView<TSharedPtr<SWidget>> TransformationLayers;
};
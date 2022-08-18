// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWaveformTransformationsOverlay.h"

#include "Widgets/SOverlay.h"

void SWaveformTransformationsOverlay::Construct(const FArguments& InArgs, TArrayView<TSharedPtr<SWidget>> InTransformationLayers)
{
	TransformationLayers = InTransformationLayers;
	CreateLayout();
}

void SWaveformTransformationsOverlay::CreateLayout()
{
	ChildSlot
	[
		SAssignNew(MainOverlayPtr, SOverlay)
	];

	for (TSharedPtr<SWidget> Layer : TransformationLayers)
	{
		if (Layer)
		{
			MainOverlayPtr->AddSlot()
			[
				Layer.ToSharedRef()
			];
		}
	}
}

void SWaveformTransformationsOverlay::OnLayerChainUpdate(TSharedPtr<SWidget>* FirstLayerPtr, const int32 NLayers)
{
	TransformationLayers = MakeArrayView(FirstLayerPtr, NLayers);
	CreateLayout();
}

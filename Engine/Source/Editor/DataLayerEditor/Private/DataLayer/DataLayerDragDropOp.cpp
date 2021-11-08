// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayer/DataLayerDragDropOp.h"
#include "Textures/SlateIcon.h"

void FDataLayerDragDropOp::Construct()
{
	const FSlateBrush* Icon = FEditorStyle::GetBrush(TEXT("DataLayer.Editor"));
	if (DataLayerLabels.Num() == 1)
	{
		SetToolTip(FText::FromName(DataLayerLabels[0]), Icon);
	}
	else
	{
		FText Text = FText::Format(NSLOCTEXT("FDataLayerDragDropOp", "MultipleFormat", "{0} DataLayerLabels"), DataLayerLabels.Num());
		SetToolTip(Text, Icon);
	}

	SetupDefaults();
	FDecoratedDragDropOp::Construct();
}

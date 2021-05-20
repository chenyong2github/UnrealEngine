// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNodalOffsetToolPanel.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "NodalOffsetTool"


void SNodalOffsetToolPanel::Construct(const FArguments& InArgs, ULensFile* InLensFile)
{
	LensFile = TStrongObjectPtr<ULensFile>(InLensFile);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TEMPPLACEHOLDER", "Nothing to show"))
		]
	];
}


#undef LOCTEXT_NAMESPACE /*NodalOffsetTool*/

// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFavoriteFilter.h"

#include "EditorStyleSet.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

void SFavoriteFilter::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.Padding(2.f)
		.BorderBackgroundColor( FLinearColor(0.5f, 0.5f, 0.5f, 0.5f) )
		.BorderImage(FEditorStyle::GetBrush("ContentBrowser.FilterButtonBorder"))
		[
			SNew(STextBlock)
				.ColorAndOpacity(FLinearColor::White)
				.Font(FEditorStyle::GetFontStyle("ContentBrowser.FilterNameFont"))
				.ShadowOffset(FVector2D(1.f, 1.f))
				.Text(InArgs._FilterName)
		]
	];
}

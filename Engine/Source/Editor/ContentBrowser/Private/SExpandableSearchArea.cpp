// Copyright Epic Games, Inc. All Rights Reserved.

#include "SExpandableSearchArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SSearchBox.h"

void SExpandableSearchArea::Construct(const FArguments& InArgs, TSharedRef<SSearchBox> SearchBox)
{
	bIsExpanded = false;

	SearchStyle = InArgs._Style;

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(0.0f, 2.0f))
			.OnClicked(this, &SExpandableSearchArea::OnExpandSearchClicked)
			[
				SNew(SImage)
				.Image(&SearchStyle->GlassImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]
		+ SHorizontalBox::Slot()
		[
			SNew(SBox)
			.Visibility(this, &SExpandableSearchArea::GetSearchBoxVisibility)
			.MinDesiredWidth(250.0f)
			.MaxDesiredWidth(250.0f)
			[
				SearchBox
			]
		]
	];
}

FReply SExpandableSearchArea::OnExpandSearchClicked()
{
	bIsExpanded = !bIsExpanded;
	return FReply::Handled();
}

EVisibility SExpandableSearchArea::GetSearchBoxVisibility() const
{
	return bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

const FSlateBrush* SExpandableSearchArea::GetExpandSearchImage() const
{
	return &SearchStyle->GlassImage;
}

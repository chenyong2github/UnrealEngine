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

	SearchBoxPtr = SearchBox;

	ChildSlot
	[
		SNew(SHorizontalBox)
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
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ToolTipText(NSLOCTEXT("ExpandableSearchArea", "ExpandCollapseSearchButton", "Expands or collapses the search text box"))
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(0.0f, 2.0f))
			.OnClicked(this, &SExpandableSearchArea::OnExpandSearchClicked)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SImage)
					.Image(this, &SExpandableSearchArea::GetExpandSearchImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SImage)
					.Image(&SearchStyle->GlassImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Visibility(this, &SExpandableSearchArea::GetSearchGlassVisibility)
				]
			]
		]
	];
}

void SExpandableSearchArea::SetExpanded(bool bInExpanded)
{
	bIsExpanded = bInExpanded;
}

FReply SExpandableSearchArea::OnExpandSearchClicked()
{
	if(TSharedPtr<SSearchBox> SearchBox = SearchBoxPtr.Pin())
	{
		bIsExpanded = !bIsExpanded;

		return FReply::Handled().SetUserFocus(SearchBox.ToSharedRef(), EFocusCause::SetDirectly);
	}

	return FReply::Handled();
}

EVisibility SExpandableSearchArea::GetSearchBoxVisibility() const
{
	return bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SExpandableSearchArea::GetSearchGlassVisibility() const
{
	return bIsExpanded ? EVisibility::Collapsed : EVisibility::Visible;
}


const FSlateBrush* SExpandableSearchArea::GetExpandSearchImage() const
{
	static const FName RightIcon("Icons.ChevronRight");

	return bIsExpanded ? FAppStyle::Get().GetBrush(RightIcon) : FStyleDefaults::GetNoBrush();
}

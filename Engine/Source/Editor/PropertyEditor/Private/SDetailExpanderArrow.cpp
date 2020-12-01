// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDetailExpanderArrow.h"
#include "SDetailTableRowBase.h"
#include "SConstrainedBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"

void SDetailExpanderArrow::Construct(const FArguments& InArgs, TSharedRef<SDetailTableRowBase> DetailsRow)
{
	Row = DetailsRow;

	ChildSlot
	[
		SNew(SConstrainedBox)
		.MinWidth(20)
		.Visibility(this, &SDetailExpanderArrow::GetExpanderVisibility)
		[
			SAssignNew(ExpanderArrow, SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.ClickMethod(EButtonClickMethod::MouseDown)
			.OnClicked(this, &SDetailExpanderArrow::OnExpanderClicked)
			.ContentPadding(FMargin(5,0,0,0))
			.IsFocusable(false)
			[
				SNew(SImage)
				.Image(this, &SDetailExpanderArrow::GetExpanderImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]
	];
}

EVisibility SDetailExpanderArrow::GetExpanderVisibility() const
{
	return Row->DoesItemHaveChildren() ? EVisibility::Visible : EVisibility::Collapsed;
}

const FSlateBrush* SDetailExpanderArrow::GetExpanderImage() const
{
	const bool bIsItemExpanded = Row->IsItemExpanded();

	FName ResourceName;
	if (bIsItemExpanded)
	{
		if (ExpanderArrow->IsHovered())
		{
			static FName ExpandedHoveredName = "TreeArrow_Expanded_Hovered";
			ResourceName = ExpandedHoveredName;
		}
		else
		{
			static FName ExpandedName = "TreeArrow_Expanded";
			ResourceName = ExpandedName;
		}
	}
	else
	{
		if (ExpanderArrow->IsHovered())
		{
			static FName CollapsedHoveredName = "TreeArrow_Collapsed_Hovered";
			ResourceName = CollapsedHoveredName;
		}
		else
		{
			static FName CollapsedName = "TreeArrow_Collapsed";
			ResourceName = CollapsedName;
		}
	}

	return FAppStyle::Get().GetBrush(ResourceName);
}

FReply SDetailExpanderArrow::OnExpanderClicked()
{
	// Recurse the expansion if "shift" is being pressed
	const FModifierKeysState ModKeyState = FSlateApplication::Get().GetModifierKeys();
	if (ModKeyState.IsShiftDown())
	{
		Row->Private_OnExpanderArrowShiftClicked();
	}
	else
	{
		Row->ToggleExpansion();
	}

	return FReply::Handled();
}
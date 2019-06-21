// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Tree/SCurveEditorTreeFilterStatusBar.h"
#include "CurveEditor.h"

#include "Algo/Count.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SHyperlink.h"

#define LOCTEXT_NAMESPACE "SCurveEditorTreeFilterStatusBar"

void SCurveEditorTreeFilterStatusBar::Construct(const FArguments& InArgs, TSharedPtr<FCurveEditor> InCurveEditor)
{
	WeakCurveEditor = InCurveEditor;

	ChildSlot
	.Padding(FMargin(5.f, 3.f))
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		[
			SAssignNew(TextBlock, STextBlock)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(FMargin(3.f, 0.f, 0.f, 0.f))
		[
			SNew(SHyperlink)
			.Visibility(this, &SCurveEditorTreeFilterStatusBar::GetVisibilityFromFilter)
			.Text(LOCTEXT("ClearFilters", "clear"))
			.OnNavigate(this, &SCurveEditorTreeFilterStatusBar::ClearFilters)
		]
	];

	InCurveEditor->GetTree()->Events.OnItemsChanged.AddSP(this, &SCurveEditorTreeFilterStatusBar::UpdateText);
	InCurveEditor->GetTree()->Events.OnSelectionChanged.AddSP(this, &SCurveEditorTreeFilterStatusBar::UpdateText);
}

void SCurveEditorTreeFilterStatusBar::ClearFilters()
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor)
	{
		CurveEditor->GetTree()->ClearFilters();
	}
}

EVisibility SCurveEditorTreeFilterStatusBar::GetVisibilityFromFilter() const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	return (CurveEditor && CurveEditor->GetTree()->GetFilterStates().IsActive()) ? EVisibility::Visible : EVisibility::Collapsed;
}

void SCurveEditorTreeFilterStatusBar::UpdateText()
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!ensureAlways(CurveEditor))
	{
		return;
	}

	FText NewText;
	FLinearColor NewColor = FLinearColor::White;

	const FCurveEditorTree* Tree = CurveEditor->GetTree();
	const FCurveEditorFilterStates& FilterStates = Tree->GetFilterStates();
	const TMap<FCurveEditorTreeItemID, ECurveEditorTreeSelectionState>& Selection = Tree->GetSelection();

	FFormatNamedArguments NamedArgs;
	NamedArgs.Add("Total", Tree->GetAllItems().Num());

	const bool bHasSelection = Selection.Num() != 0;
	const bool bHasFilter    = FilterStates.IsActive();

	if (bHasSelection)
	{
		const int32 NumExplicitlySelected = Algo::CountIf(Selection, [](TTuple<FCurveEditorTreeItemID, ECurveEditorTreeSelectionState> Pair){ return Pair.Value == ECurveEditorTreeSelectionState::Explicit; });
		NamedArgs.Add("NumSelected", NumExplicitlySelected);
	}

	if (bHasFilter)
	{
		NamedArgs.Add("NumMatched", FilterStates.GetNumMatched() + FilterStates.GetNumMatchedImplicitly());
	}


	if (bHasFilter)
	{
		if (FilterStates.GetNumMatched() == 0)
		{
			// Red = no matched
			NewColor = FLinearColor( 1.0f, 0.4f, 0.4f );
		}
		else
		{
			// Green = matched filter
			NewColor = FLinearColor( 0.4f, 1.0f, 0.4f );
		}

		if (bHasSelection)
		{
			NewText = FText::Format(LOCTEXT("FilteredStatus_WithSelection", "Showing {NumMatched} of {Total} items ({NumSelected} selected)"), NamedArgs);
		}
		else 
		{
			NewText = FText::Format(LOCTEXT("FilteredStatus_NoSelection", "Showing {NumMatched} of {Total} items"), NamedArgs);
		}
	}
	else if (bHasSelection)
	{
		NewText = FText::Format(LOCTEXT("UnfilteredStatus_WithSelection", "{Total} items ({NumSelected} selected)"), NamedArgs);
	}
	else
	{
		NewText = FText::Format(LOCTEXT("UnfilteredStatus_NoSelection", "{Total} items"), NamedArgs);
	}

	TextBlock->SetColorAndOpacity(NewColor);
	TextBlock->SetText(NewText);
}

#undef LOCTEXT_NAMESPACE
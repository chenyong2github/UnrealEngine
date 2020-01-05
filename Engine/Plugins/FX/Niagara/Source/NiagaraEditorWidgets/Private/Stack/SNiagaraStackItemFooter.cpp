// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackItemFooter.h"
#include "ViewModels/Stack/NiagaraStackItemFooter.h"
#include "EditorStyleSet.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "NiagaraStackItemExpander"

void SNiagaraStackItemFooter::Construct(const FArguments& InArgs, UNiagaraStackItemFooter& InItemFooter)
{
	ItemFooter = &InItemFooter;
	ExpandedToolTipText = LOCTEXT("HideAdvancedToolTip", "Hide Advanced");
	CollapsedToolTipText = LOCTEXT("ShowAdvancedToolTip", "Show Advanced");

	ChildSlot
	[
		SNew(SButton)
		.ButtonStyle(FEditorStyle::Get(), "NoBorder")
		.Visibility(this, &SNiagaraStackItemFooter::GetExpandButtonVisibility)
		.HAlign(HAlign_Center)
		.ContentPadding(2)
		.ToolTipText(this, &SNiagaraStackItemFooter::GetToolTipText)
		.OnClicked(this, &SNiagaraStackItemFooter::ExpandButtonClicked)
		.IsFocusable(false)
		.Content()
		[
			SNew(SImage)
			.Image(this, &SNiagaraStackItemFooter::GetButtonBrush)
		]
	];
}

EVisibility SNiagaraStackItemFooter::GetExpandButtonVisibility() const
{
	return ItemFooter->GetHasAdvancedContent() ? EVisibility::Visible : EVisibility::Hidden;
}

const FSlateBrush* SNiagaraStackItemFooter::GetButtonBrush() const
{
	if (IsHovered())
	{
		return ItemFooter->GetShowAdvanced()
			? FEditorStyle::GetBrush("DetailsView.PulldownArrow.Up.Hovered")
			: FEditorStyle::GetBrush("DetailsView.PulldownArrow.Down.Hovered");
	}
	else
	{
		return ItemFooter->GetShowAdvanced()
			? FEditorStyle::GetBrush("DetailsView.PulldownArrow.Up")
			: FEditorStyle::GetBrush("DetailsView.PulldownArrow.Down");
	}
}

FText SNiagaraStackItemFooter::GetToolTipText() const
{
	// The engine ticks tooltips before widgets so it's possible for the footer to be finalized when
	// the widgets haven't been recreated.
	if (ItemFooter->IsFinalized())
	{
		return FText();
	}
	return ItemFooter->GetShowAdvanced() ? ExpandedToolTipText : CollapsedToolTipText;
}

FReply SNiagaraStackItemFooter::ExpandButtonClicked()
{
	ItemFooter->ToggleShowAdvanced();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
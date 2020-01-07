// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackItem.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorStyle.h"
#include "EditorStyleSet.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "SNiagaraStackIssueIcon.h"

#define LOCTEXT_NAMESPACE "NiagaraStackItem"

void SNiagaraStackItem::Construct(const FArguments& InArgs, UNiagaraStackItem& InItem, UNiagaraStackViewModel* InStackViewModel)
{
	Item = &InItem;
	StackEntryItem = Item;
	StackViewModel = InStackViewModel;

	TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);

	// Icon
	if (Item->SupportsIcon())
	{
		RowBox->AddSlot()
		.Padding(2, 0, 3, 0)
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Image(Item->GetIconBrush())
		];
	}

	// Display name
	RowBox->AddSlot()
	.Padding(2, 0, 2, 0)
	.VAlign(VAlign_Center)
	[
		SNew(SNiagaraStackDisplayName, InItem, *InStackViewModel, "NiagaraEditor.Stack.ItemText")
	];

	// Reset to base button
	if (Item->SupportsResetToBase())
	{
		RowBox->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 2, 0)
		[
			SNew(SButton)
			.Visibility(this, &SNiagaraStackItem::GetResetToBaseButtonVisibility)
			.IsFocusable(false)
			.ToolTipText(this, &SNiagaraStackItem::GetResetToBaseButtonToolTipText)
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ContentPadding(0)
			.OnClicked(this, &SNiagaraStackItem::ResetToBaseButtonClicked)
			.Content()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
				.ColorAndOpacity(FSlateColor(FLinearColor::Green))
			]
		];
	}

	// Delete button
	if (Item->SupportsDelete())
	{
		RowBox->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 2, 0)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.IsFocusable(false)
			.ForegroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.FlatButtonColor"))
			.ToolTipText(this, &SNiagaraStackItem::GetDeleteButtonToolTipText)
			.IsEnabled(this, &SNiagaraStackItem::GetDeleteButtonEnabled)
			.OnClicked(this, &SNiagaraStackItem::DeleteClicked)
			.Content()
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FText::FromString(FString(TEXT("\xf1f8"))))
			]
		];
	}

	// Enabled checkbox
	if (Item->SupportsChangeEnabled())
	{
		RowBox->AddSlot()
		.Padding(0)
		.AutoWidth()
		[
			SNew(SCheckBox)
			.IsChecked(this, &SNiagaraStackItem::CheckEnabledStatus)
			.OnCheckStateChanged(this, &SNiagaraStackItem::OnCheckStateChanged)
		];
	}

	ChildSlot
	[
		RowBox
	];
}

EVisibility SNiagaraStackItem::GetResetToBaseButtonVisibility() const
{
	FText Unused;
	return Item->TestCanResetToBaseWithMessage(Unused) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SNiagaraStackItem::GetResetToBaseButtonToolTipText() const
{
	FText CanResetToBaseMessage;
	Item->TestCanCopyWithMessage(CanResetToBaseMessage);
	return CanResetToBaseMessage;
}

FReply SNiagaraStackItem::ResetToBaseButtonClicked()
{
	Item->ResetToBase();
	return FReply::Handled();
}

FText SNiagaraStackItem::GetDeleteButtonToolTipText() const
{
	FText CanDeleteMessage;
	Item->TestCanDeleteWithMessage(CanDeleteMessage);
	return CanDeleteMessage;
}

bool SNiagaraStackItem::GetDeleteButtonEnabled() const
{
	FText Unused;
	return Item->TestCanDeleteWithMessage(Unused);
}

FReply SNiagaraStackItem::DeleteClicked()
{
	Item->Delete();
	return FReply::Handled();
}

void SNiagaraStackItem::OnCheckStateChanged(ECheckBoxState InCheckState)
{
	Item->SetIsEnabled(InCheckState == ECheckBoxState::Checked);
}

ECheckBoxState SNiagaraStackItem::CheckEnabledStatus() const
{
	return Item->GetIsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

#undef LOCTEXT_NAMESPACE
// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackRendererItem.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorStyle.h"
#include "EditorStyleSet.h"
#include "NiagaraRendererProperties.h"
#include "ViewModels/Stack/NiagaraStackRendererItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "SNiagaraStackIssueIcon.h"

#define LOCTEXT_NAMESPACE "NiagaraStackRendererItem"

void SNiagaraStackRendererItem::Construct(const FArguments& InArgs, UNiagaraStackRendererItem& InRendererItem, UNiagaraStackViewModel* InStackViewModel)
{
	RendererItem = &InRendererItem;
	StackEntryItem = RendererItem;
	StackViewModel = InStackViewModel;

	ChildSlot
	[
		SNew(SHorizontalBox)
		// Renderer icon
		+ SHorizontalBox::Slot()
		.Padding(2, 0, 0, 0)
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Image(FSlateIconFinder::FindIconBrushForClass(RendererItem->GetRendererProperties()->GetClass()))
		]
		// Display name
		+ SHorizontalBox::Slot()
		.Padding(2, 0, 0, 0)
		.VAlign(VAlign_Center)
		[
			SNew(SNiagaraStackDisplayName, InRendererItem, *InStackViewModel, "NiagaraEditor.Stack.ItemText")
			.ColorAndOpacity(this, &SNiagaraStackEntryWidget::GetTextColorForSearch)
		]
		// Reset to base Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(3, 0, 0, 0)
		[
			SNew(SButton)
			.IsFocusable(false)
			.ToolTipText(LOCTEXT("ResetRendererToBaseToolTip", "Reset this renderer to the state defined by the parent emitter"))
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ContentPadding(0)
			.Visibility(this, &SNiagaraStackRendererItem::GetResetToBaseButtonVisibility)
			.OnClicked(this, &SNiagaraStackRendererItem::ResetToBaseButtonClicked)
			.Content()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
				.ColorAndOpacity(FSlateColor(FLinearColor::Green))
			]
		]
		// Delete button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.IsFocusable(false)
			.ForegroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.FlatButtonColor"))
			.ToolTipText(this, &SNiagaraStackRendererItem::GetDeleteButtonToolTipText)
			.IsEnabled(this, &SNiagaraStackRendererItem::GetDeleteButtonEnabled)
			.OnClicked(this, &SNiagaraStackRendererItem::DeleteClicked)
			.Content()
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FText::FromString(FString(TEXT("\xf1f8"))))
			]
		]
		// Enabled checkbox
		+ SHorizontalBox::Slot()
		.Padding(2, 0, 0, 0)
		.AutoWidth()
		[
			SNew(SCheckBox)
			.IsChecked(this, &SNiagaraStackRendererItem::CheckEnabledStatus)
			.OnCheckStateChanged(this, &SNiagaraStackRendererItem::OnCheckStateChanged)
		]
	];
}

FText SNiagaraStackRendererItem::GetDeleteButtonToolTipText() const
{
	FText CanDeleteMessage;
	RendererItem->TestCanDeleteWithMessage(CanDeleteMessage);
	return CanDeleteMessage;
}

bool SNiagaraStackRendererItem::GetDeleteButtonEnabled() const
{
	FText CanDeleteMessage;
	return RendererItem->TestCanDeleteWithMessage(CanDeleteMessage);
}

FReply SNiagaraStackRendererItem::DeleteClicked()
{
	RendererItem->Delete();
	return FReply::Handled();
}

EVisibility SNiagaraStackRendererItem::GetResetToBaseButtonVisibility() const
{
	if (RendererItem->HasBaseEmitter())
	{
		return RendererItem->CanResetToBase() ? EVisibility::Visible : EVisibility::Hidden;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

FReply SNiagaraStackRendererItem::ResetToBaseButtonClicked()
{
	RendererItem->ResetToBase();
	return FReply::Handled();
}

void SNiagaraStackRendererItem::OnCheckStateChanged(ECheckBoxState InCheckState)
{
	RendererItem->SetIsEnabled(InCheckState == ECheckBoxState::Checked);
}

ECheckBoxState SNiagaraStackRendererItem::CheckEnabledStatus() const
{
	return RendererItem->GetIsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

#undef LOCTEXT_NAMESPACE
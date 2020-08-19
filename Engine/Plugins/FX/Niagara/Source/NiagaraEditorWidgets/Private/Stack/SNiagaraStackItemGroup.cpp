// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackItemGroup.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorStyle.h"
#include "EditorStyleSet.h"
#include "Stack/SNiagaraStackItemGroupAddButton.h"
#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "NiagaraStackItemGroup"


void SNiagaraStackItemGroup::Construct(const FArguments& InArgs, UNiagaraStackItemGroup& InGroup, UNiagaraStackViewModel* InStackViewModel)
{
	Group = &InGroup;
	StackEntryItem = Group;
	StackViewModel = InStackViewModel;

	TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);

	// Name
	RowBox->AddSlot()
	.VAlign(VAlign_Center)
	.Padding(2, 0, 0, 0)
	[
		SNew(SNiagaraStackDisplayName, InGroup, *InStackViewModel)
		.NameStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.GroupText")
	];

	// Delete group button
	RowBox->AddSlot()
	.AutoWidth()
	[
		SNew(SButton)
		.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
		.IsFocusable(false)
		.ForegroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.ForegroundColor"))
		.ToolTipText(this, &SNiagaraStackItemGroup::GetDeleteButtonToolTip)
		.OnClicked(this, &SNiagaraStackItemGroup::DeleteClicked)
		.IsEnabled(this, &SNiagaraStackItemGroup::GetDeleteButtonIsEnabled)
		.Visibility(this, &SNiagaraStackItemGroup::GetDeleteButtonVisibility)
		.Content()
		[
			SNew(STextBlock)
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
			.Text(FText::FromString(FString(TEXT("\xf1f8"))))
		]
	];

	// Enabled button
	if (Group->SupportsChangeEnabled())
	{
		RowBox->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(0, 0, 0, 0)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SNiagaraStackItemGroup::CheckEnabledStatus)
			.OnCheckStateChanged(this, &SNiagaraStackItemGroup::OnCheckStateChanged)
			.IsEnabled(this, &SNiagaraStackItemGroup::GetEnabledCheckBoxEnabled)
		];
	}

	// Add button
	RowBox->AddSlot()
	.AutoWidth()
	.HAlign(HAlign_Right)
	.Padding(2, 0, 0, 0)
	[
		ConstructAddButton()
	];

	ChildSlot
	[
		RowBox
	];
}

TSharedRef<SWidget> SNiagaraStackItemGroup::ConstructAddButton()
{
	INiagaraStackItemGroupAddUtilities* AddUtilities = Group->GetAddUtilities();
	if (AddUtilities != nullptr)
	{
		return SNew(SNiagaraStackItemGroupAddButton, *Group);
	}
	return SNullWidget::NullWidget;
}

FText SNiagaraStackItemGroup::GetDeleteButtonToolTip() const
{
	FText Message;
	Group->TestCanDeleteWithMessage(Message);
	return Message;
}

bool SNiagaraStackItemGroup::GetDeleteButtonIsEnabled() const
{
	FText UnusedMessage;
	return Group->TestCanDeleteWithMessage(UnusedMessage);
}

EVisibility SNiagaraStackItemGroup::GetDeleteButtonVisibility() const
{
	return Group->SupportsDelete() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SNiagaraStackItemGroup::DeleteClicked()
{
	Group->Delete();
	return FReply::Handled();
}

void SNiagaraStackItemGroup::OnCheckStateChanged(ECheckBoxState InCheckState)
{
	Group->SetIsEnabled(InCheckState == ECheckBoxState::Checked);
}

ECheckBoxState SNiagaraStackItemGroup::CheckEnabledStatus() const
{
	return Group->GetIsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool SNiagaraStackItemGroup::GetEnabledCheckBoxEnabled() const
{
	return Group->GetOwnerIsEnabled();
}

#undef LOCTEXT_NAMESPACE

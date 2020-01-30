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
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "NiagaraStackItemGroup"


void SNiagaraStackItemGroup::Construct(const FArguments& InArgs, UNiagaraStackItemGroup& InGroup, UNiagaraStackViewModel* InStackViewModel)
{
	Group = &InGroup;
	StackEntryItem = Group;
	StackViewModel = InStackViewModel;

	ChildSlot
	[
		SNew(SHorizontalBox)
		// Name
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(2, 0, 0, 0)
		[
			SNew(SNiagaraStackDisplayName, InGroup, *InStackViewModel, "NiagaraEditor.Stack.GroupText")
		]
		// Delete group button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.IsFocusable(false)
			.ForegroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.ForegroundColor"))
			.ToolTipText(this, &SNiagaraStackItemGroup::GetDeleteButtonToolTip)
			.OnClicked(this, &SNiagaraStackItemGroup::DeleteClicked)
			.IsEnabled(this, &SNiagaraStackItemGroup::GetDeleteButtonIsEnabled)
			.Content()
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FText::FromString(FString(TEXT("\xf1f8"))))
			]
		]
		// Add button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		[
			ConstructAddButton()
		]
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

FReply SNiagaraStackItemGroup::DeleteClicked()
{
	Group->Delete();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
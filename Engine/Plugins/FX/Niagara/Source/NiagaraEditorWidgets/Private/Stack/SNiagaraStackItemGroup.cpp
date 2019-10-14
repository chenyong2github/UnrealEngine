// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
#include "SNiagaraStackErrorButton.h"

#define LOCTEXT_NAMESPACE "NiagaraStackItemGroup"


void SNiagaraStackItemGroup::Construct(const FArguments& InArgs, UNiagaraStackItemGroup& InGroup, UNiagaraStackViewModel* InStackViewModel)
{
	Group = &InGroup;
	StackEntryItem = Group;
	StackViewModel = InStackViewModel;

	ChildSlot
	[
		// Name
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(SNiagaraStackDisplayName, InGroup, *InStackViewModel, "NiagaraEditor.Stack.GroupText")
			.ColorAndOpacity(this, &SNiagaraStackEntryWidget::GetTextColorForSearch)
		]
		// Stack issues icon
		+ SHorizontalBox::Slot()
		.Padding(2, 0, 0, 0)
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		[
			SNew(SNiagaraStackErrorButton)
			.IssueSeverity_UObject(Group, &UNiagaraStackItemGroup::GetHighestStackIssueSeverity)
			.ErrorTooltip(this, &SNiagaraStackItemGroup::GetErrorButtonTooltipText)
			.Visibility(this, &SNiagaraStackItemGroup::GetStackIssuesWarningVisibility)
			.OnButtonClicked(this, &SNiagaraStackItemGroup::ExpandEntry)
		]
		// Delete group button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Visibility(this, &SNiagaraStackItemGroup::GetDeleteButtonVisibility)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.IsFocusable(false)
			.ForegroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.ForegroundColor"))
			.ToolTipText(LOCTEXT("DeleteGroupToolTip", "Delete this group"))
			.OnClicked(this, &SNiagaraStackItemGroup::DeleteClicked)
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

EVisibility SNiagaraStackItemGroup::GetDeleteButtonVisibility() const
{
	if (Group->CanDelete())
	{
		return EVisibility::Visible;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

FReply SNiagaraStackItemGroup::DeleteClicked()
{
	Group->Delete();
	return FReply::Handled();
}

EVisibility SNiagaraStackItemGroup::GetStackIssuesWarningVisibility() const
{
	return  Group->GetRecursiveStackIssuesCount() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SNiagaraStackItemGroup::GetErrorButtonTooltipText() const
{
	return FText::Format(LOCTEXT("GroupIssuesTooltip", "This group contains items that have a total of {0} issues, click to expand."), Group->GetRecursiveStackIssuesCount());
}

#undef LOCTEXT_NAMESPACE
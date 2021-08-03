// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackItemGroupAddButton.h"
#include "Stack/SNiagaraStackItemGroupAddMenu.h"
#include "ViewModels/Stack/INiagaraStackItemGroupAddUtilities.h"
#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "NiagaraEditorWidgetsUtilities.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "EditorStyleSet.h"

#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SNiagaraStackItemGroupAddButton"

const float SNiagaraStackItemGroupAddButton::TextIconSize = 16;

void SNiagaraStackItemGroupAddButton::Construct(const FArguments& InArgs, UNiagaraStackItemGroup& InStackItemGroup)
{
	TSharedPtr<SWidget> Content;
	if(InStackItemGroup.GetAddUtilities() != nullptr)
	{
		INiagaraStackItemGroupAddUtilities* AddUtilities = InStackItemGroup.GetAddUtilities();
		FText AddToGroupFormat = LOCTEXT("AddToGroupFormat", "Add a new {0} to this group.");
		if (AddUtilities->GetAddMode() == INiagaraStackItemGroupAddUtilities::AddFromAction)
		{
			Content = SAssignNew(AddActionButton, SComboButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(FText::Format(AddToGroupFormat, AddUtilities->GetAddItemName()))
				.HasDownArrow(false)
				.OnGetMenuContent(this, &SNiagaraStackItemGroupAddButton::GetAddMenu)
				.IsEnabled_UObject(&InStackItemGroup, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				.ContentPadding(1.0f)
				.MenuPlacement(MenuPlacement_BelowRightAnchor)
				.ButtonContent()
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
					.ColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(InStackItemGroup.GetExecutionCategoryName())))
				];
		}
		else if (AddUtilities->GetAddMode() == INiagaraStackItemGroupAddUtilities::AddDirectly)
		{
			Content = SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(FText::Format(AddToGroupFormat, AddUtilities->GetAddItemName()))
				.IsEnabled_UObject(&InStackItemGroup, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				.ContentPadding(1.0f)
				.OnClicked(this, &SNiagaraStackItemGroupAddButton::AddDirectlyButtonClicked)
				.Content()
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
					.ColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(InStackItemGroup.GetExecutionCategoryName())))
				];
		}
	}
	else
	{
		// TODO Log error here.
		Content = SNullWidget::NullWidget;
	}

	ChildSlot
	[
		Content.ToSharedRef()
	];
	
	StackItemGroupWeak = &InStackItemGroup;
}

TSharedRef<SWidget> SNiagaraStackItemGroupAddButton::GetAddMenu()
{
	if(StackItemGroupWeak.IsValid())
	{
		TSharedRef<SNiagaraStackItemGroupAddMenu> AddMenu = SNew(SNiagaraStackItemGroupAddMenu, StackItemGroupWeak, StackItemGroupWeak->GetAddUtilities(), INDEX_NONE);
		AddActionButton->SetMenuContentWidgetToFocus(AddMenu->GetFilterTextBox()->AsShared());
		return AddMenu;
	}
	return SNullWidget::NullWidget;
}

FReply SNiagaraStackItemGroupAddButton::AddDirectlyButtonClicked()
{
	if(StackItemGroupWeak.IsValid())
	{
		StackItemGroupWeak->GetAddUtilities()->AddItemDirectly();
		StackItemGroupWeak->SetIsExpandedInOverview(true);
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
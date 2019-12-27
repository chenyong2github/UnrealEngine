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
				.ButtonStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.AddButton")
				.ButtonColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(InStackItemGroup.GetExecutionCategoryName())))
				.ToolTipText(FText::Format(AddToGroupFormat, AddUtilities->GetAddItemName()))
				.HasDownArrow(false)
				.OnGetMenuContent(this, &SNiagaraStackItemGroupAddButton::GetAddMenu)
				.IsEnabled_UObject(&InStackItemGroup, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				.ContentPadding(0)
				.MenuPlacement(MenuPlacement_BelowRightAnchor)
				.ButtonContent()
				[
					SNew(SBox)
					.WidthOverride(InArgs._Width)
					.HeightOverride(TextIconSize)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						//.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.AddButtonText")
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
						.Text(FText::FromString(FString(TEXT("\xf067"))) /*fa-plus*/)
					]
				];
		}
		else if (AddUtilities->GetAddMode() == INiagaraStackItemGroupAddUtilities::AddDirectly)
		{
			Content = SNew(SButton)
				.ButtonStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.AddButton")
				.ButtonColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(InStackItemGroup.GetExecutionCategoryName())))
				.ToolTipText(FText::Format(AddToGroupFormat, AddUtilities->GetAddItemName()))
				.IsEnabled_UObject(&InStackItemGroup, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				.ContentPadding(0)
				.OnClicked(this, &SNiagaraStackItemGroupAddButton::AddDirectlyButtonClicked)
				.Content()
				[
					SNew(SBox)
					.WidthOverride(InArgs._Width)
					.HeightOverride(TextIconSize)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						//.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.AddButtonText")
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
						.Text(FText::FromString(FString(TEXT("\xf067"))) /*fa-plus*/)
					]
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
		TSharedRef<SNiagaraStackItemGroupAddMenu> AddMenu = SNew(SNiagaraStackItemGroupAddMenu, StackItemGroupWeak->GetAddUtilities(), INDEX_NONE);
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
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
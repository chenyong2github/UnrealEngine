// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Stack/SNiagaraStackItemGroupAddMenu.h"
#include "EditorStyleSet.h"
#include "NiagaraActions.h"
#include "ViewModels/Stack/INiagaraStackItemGroupAddUtilities.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "EdGraph/EdGraph.h"
#include "SGraphActionMenu.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SCheckBox.h"
#include "Styling/SlateTypes.h"

#define LOCTEXT_NAMESPACE "NiagaraStackItemGroupAddMenu"

bool SNiagaraStackItemGroupAddMenu::bIncludeNonLibraryScripts = false;

void SNiagaraStackItemGroupAddMenu::Construct(const FArguments& InArgs, INiagaraStackItemGroupAddUtilities* InAddUtilities, int32 InInsertIndex)
{
	AddUtilities = InAddUtilities;
	InsertIndex = InInsertIndex;
	bSetFocusOnNextTick = true;
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			.WidthOverride(300)
			.HeightOverride(400)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.Padding(1.0f)
				[
					SNew(SHorizontalBox)

					// Search context description
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("AddToGroupFormatTitle", "Add new {0}"), AddUtilities->GetAddItemName()))
					]

					// Library Only Toggle
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.OnCheckStateChanged(this, &SNiagaraStackItemGroupAddMenu::OnLibraryToggleChanged)
						.IsChecked(this, &SNiagaraStackItemGroupAddMenu::LibraryToggleIsChecked)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("LibraryOnly", "Library Only"))
						]
					]
				]
				+SVerticalBox::Slot()
				.FillHeight(15)
				[
					SAssignNew(AddMenu, SGraphActionMenu)
					.OnActionSelected(this, &SNiagaraStackItemGroupAddMenu::OnActionSelected)
					.OnCollectAllActions(this, &SNiagaraStackItemGroupAddMenu::CollectAllAddActions)
					.AutoExpandActionMenu(AddUtilities->GetAutoExpandAddActions())
					.ShowFilterTextBox(true)
				]
			]
		]
	];
}

void SNiagaraStackItemGroupAddMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bSetFocusOnNextTick)
	{
		bSetFocusOnNextTick = false;
		FSlateApplication::Get().SetKeyboardFocus(GetFilterTextBox(), EFocusCause::SetDirectly);
	}
}

TSharedPtr<SEditableTextBox> SNiagaraStackItemGroupAddMenu::GetFilterTextBox()
{
	return AddMenu->GetFilterTextBox();
}

void SNiagaraStackItemGroupAddMenu::CollectAllAddActions(FGraphActionListBuilderBase& OutAllActions)
{
	if (OutAllActions.OwnerOfTemporaries == nullptr)
	{
		OutAllActions.OwnerOfTemporaries = NewObject<UEdGraph>((UObject*)GetTransientPackage());
	}

	FNiagaraStackItemGroupAddOptions AddOptions;
	AddOptions.bIncludeDeprecated = false;
	AddOptions.bIncludeNonLibrary = bIncludeNonLibraryScripts;

	TArray<TSharedRef<INiagaraStackItemGroupAddAction>> AddActions;
	AddUtilities->GenerateAddActions(AddActions, AddOptions);

	for (TSharedRef<INiagaraStackItemGroupAddAction> AddAction : AddActions)
	{
		TSharedPtr<FNiagaraMenuAction> NewNodeAction(
			new FNiagaraMenuAction(AddAction->GetCategory(), AddAction->GetDisplayName(), AddAction->GetDescription(), 0, AddAction->GetKeywords(),
				FNiagaraMenuAction::FOnExecuteStackAction::CreateRaw(AddUtilities, &INiagaraStackItemGroupAddUtilities::ExecuteAddAction, AddAction, InsertIndex)));
		OutAllActions.AddAction(NewNodeAction);
	}
}

void SNiagaraStackItemGroupAddMenu::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FNiagaraMenuAction> CurrentAction = StaticCastSharedPtr<FNiagaraMenuAction>(SelectedActions[ActionIndex]);

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				CurrentAction->ExecuteAction();
			}
		}
	}
}

void SNiagaraStackItemGroupAddMenu::OnLibraryToggleChanged(ECheckBoxState CheckState)
{
	SNiagaraStackItemGroupAddMenu::bIncludeNonLibraryScripts = CheckState == ECheckBoxState::Unchecked;
	AddMenu->RefreshAllActions(true, false);
}

ECheckBoxState SNiagaraStackItemGroupAddMenu::LibraryToggleIsChecked() const
{
	return SNiagaraStackItemGroupAddMenu::bIncludeNonLibraryScripts ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
}

#undef LOCTEXT_NAMESPACE
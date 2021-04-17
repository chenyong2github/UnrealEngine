// Copyright Epic Games, Inc. All Rights Reserved.
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
#include "Widgets/SNiagaraLibraryOnlyToggleHeader.h"

#define LOCTEXT_NAMESPACE "NiagaraStackItemGroupAddMenu"

bool SNiagaraStackItemGroupAddMenu::bLibraryOnly = true;

void SNiagaraStackItemGroupAddMenu::Construct(const FArguments& InArgs, INiagaraStackItemGroupAddUtilities* InAddUtilities, int32 InInsertIndex)
{
	TSharedPtr<SNiagaraLibraryOnlyToggleHeader> LibraryOnlyToggle;

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
					SAssignNew(LibraryOnlyToggle, SNiagaraLibraryOnlyToggleHeader)
					.HeaderLabelText(FText::Format(LOCTEXT("AddToGroupFormatTitle", "Add new {0}"), AddUtilities->GetAddItemName()))
					.LibraryOnly(this, &SNiagaraStackItemGroupAddMenu::GetLibraryOnly)
					.LibraryOnlyChanged(this, &SNiagaraStackItemGroupAddMenu::SetLibraryOnly)
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

	LibraryOnlyToggle->SetActionMenu(AddMenu.ToSharedRef());
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
	AddOptions.bIncludeNonLibrary = bLibraryOnly == false;

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

bool SNiagaraStackItemGroupAddMenu::GetLibraryOnly() const
{
	return bLibraryOnly;
}

void SNiagaraStackItemGroupAddMenu::SetLibraryOnly(bool bInLibraryOnly)
{
	bLibraryOnly = bInLibraryOnly;
}

#undef LOCTEXT_NAMESPACE
// Copyright Epic Games, Inc. All Rights Reserved.


#include "Editor/SControlRigGizmoNameList.h"
#include "Graph/SControlRigGraphPinNameListValueWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "DetailLayoutBuilder.h"
#include "ControlRigBlueprint.h"

void SControlRigGizmoNameList::Construct(const FArguments& InArgs, FRigControl* InControl, UControlRigBlueprint* InBlueprint)
{
	this->OnGetNameListContent = InArgs._OnGetNameListContent;
	this->ControlKey = InControl->GetElementKey();
	this->Blueprint = InBlueprint;

	SBox::Construct(SBox::FArguments());

	TSharedPtr<FString> InitialSelected;
	for (TSharedPtr<FString> Item : GetNameList())
	{
		if (Item->Equals(GetNameListText().ToString()))
		{
			InitialSelected = Item;
		}
	}

	SetContent(
		SNew(SBox)
		.MinDesiredWidth(150)
		.MaxDesiredWidth(400)
		[
			SAssignNew(NameListComboBox, SControlRigGraphPinNameListValueWidget)
			.OptionsSource(&GetNameList())
			.OnGenerateWidget(this, &SControlRigGizmoNameList::MakeNameListItemWidget)
			.OnSelectionChanged(this, &SControlRigGizmoNameList::OnNameListChanged)
			.OnComboBoxOpening(this, &SControlRigGizmoNameList::OnNameListComboBox)
			.InitiallySelectedItem(InitialSelected)
			.Content()
			[
				SNew(STextBlock)
				.Text(this, &SControlRigGizmoNameList::GetNameListText)
			]
		]
	);
}

const TArray<TSharedPtr<FString>>& SControlRigGizmoNameList::GetNameList() const
{
	if (OnGetNameListContent.IsBound())
	{
		return OnGetNameListContent.Execute();
	}
	return EmptyList;
}

FText SControlRigGizmoNameList::GetNameListText() const
{
	int32 ControlIndex = Blueprint->HierarchyContainer.GetIndex(ControlKey);
	if (ControlIndex != INDEX_NONE)
	{
		return FText::FromName(Blueprint->HierarchyContainer.ControlHierarchy[ControlIndex].GizmoName);
	}
	return FText();
}

void SControlRigGizmoNameList::SetNameListText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/)
{
	int32 ControlIndex = Blueprint->HierarchyContainer.GetIndex(ControlKey);
	if (ControlIndex != INDEX_NONE)
	{
		FName NewName = *NewTypeInValue.ToString();
		FRigControl& Control = Blueprint->HierarchyContainer.ControlHierarchy[ControlIndex];
		if (Control.GizmoName != NewName)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("ControlRigEditor", "ChangeGizmoName", "Change Gizmo Name"));
			Blueprint->Modify();
			Control.GizmoName = NewName;
			Blueprint->HierarchyContainer.ControlHierarchy.OnControlUISettingsChanged.Broadcast(&Blueprint->HierarchyContainer, ControlKey);
		}
	}
}

TSharedRef<SWidget> SControlRigGizmoNameList::MakeNameListItemWidget(TSharedPtr<FString> InItem)
{
	return 	SNew(STextBlock).Text(FText::FromString(*InItem));// .Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
}

void SControlRigGizmoNameList::OnNameListChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		FString NewValue = *NewSelection.Get();
		SetNameListText(FText::FromString(NewValue), ETextCommit::OnEnter);
	}
}

void SControlRigGizmoNameList::OnNameListComboBox()
{
	TSharedPtr<FString> CurrentlySelected;
	for (TSharedPtr<FString> Item : GetNameList())
	{
		if (Item->Equals(GetNameListText().ToString()))
		{
			CurrentlySelected = Item;
		}
	}
	NameListComboBox->SetSelectedItem(CurrentlySelected);
}

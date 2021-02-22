// Copyright Epic Games, Inc. All Rights Reserved.


#include "Editor/SControlRigGizmoNameList.h"
#include "Graph/SControlRigGraphPinNameListValueWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "DetailLayoutBuilder.h"
#include "ControlRigBlueprint.h"
#include "ControlRig.h"

void SControlRigGizmoNameList::Construct(const FArguments& InArgs, FRigControlElement* ControlElement, UControlRigBlueprint* InBlueprint)
{
	this->OnGetNameListContent = InArgs._OnGetNameListContent;
	this->ControlKey = ControlElement->GetKey();
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
	int32 ControlIndex = Blueprint->Hierarchy->GetIndex(ControlKey);
	if (ControlIndex != INDEX_NONE)
	{
		return FText::FromName(Blueprint->Hierarchy->GetChecked<FRigControlElement>(ControlIndex)->Settings.GizmoName);
	}
	return FText();
}

void SControlRigGizmoNameList::SetNameListText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/)
{
	int32 ControlIndex = Blueprint->Hierarchy->GetIndex(ControlKey);
	if (ControlIndex != INDEX_NONE)
	{
		FName NewName = *NewTypeInValue.ToString();

		bool bIsOnInstance = false;
		URigHierarchy* Hierarchy = Blueprint->Hierarchy;
		UControlRig* DebuggedControlRig = Cast<UControlRig>(Blueprint->GetObjectBeingDebugged());

		if (DebuggedControlRig)
		{
			if (!DebuggedControlRig->IsSetupModeEnabled())
			{
				Hierarchy = DebuggedControlRig->GetHierarchy();
				bIsOnInstance = true;
			}
		}

		FRigControlElement* ControlElement = Hierarchy->Get<FRigControlElement>(ControlIndex);
		if ((ControlElement != nullptr) && (ControlElement->Settings.GizmoName != NewName))
		{
			const FScopedTransaction Transaction(NSLOCTEXT("ControlRigEditor", "ChangeGizmoName", "Change Gizmo Name"));
			Blueprint->Modify();
			
			ControlElement->Settings.GizmoName = NewName;

			if (bIsOnInstance && DebuggedControlRig)
			{
				Blueprint->PropagatePropertyFromInstanceToBP(ControlKey, FRigControl::StaticStruct()->FindPropertyByName(TEXT("GizmoName")), DebuggedControlRig);
			}

			Blueprint->PropagatePropertyFromBPToInstances(ControlKey, FRigControl::StaticStruct()->FindPropertyByName(TEXT("GizmoName")));

			Blueprint->Hierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, Blueprint->Hierarchy->Find(ControlElement->GetKey()));
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

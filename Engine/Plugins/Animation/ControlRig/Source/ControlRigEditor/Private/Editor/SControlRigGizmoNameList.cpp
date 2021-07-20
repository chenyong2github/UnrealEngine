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
	TArray<FRigControlElement*> ControlElements;
	ControlElements.Add(ControlElement);
	return Construct(InArgs, ControlElements, InBlueprint);
}

void SControlRigGizmoNameList::Construct(const FArguments& InArgs, TArray<FRigControlElement*> ControlElements, UControlRigBlueprint* InBlueprint)
{
	this->OnGetNameListContent = InArgs._OnGetNameListContent;
	this->ControlKeys.Reset();

	for(FRigControlElement* ControlElement : ControlElements)
	{
		this->ControlKeys.Add(ControlElement->GetKey());
	}
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
	FName FirstName = NAME_None;
	FText Text;
	for(int32 KeyIndex = 0; KeyIndex < ControlKeys.Num(); KeyIndex++)
	{
		const int32 ControlIndex = Blueprint->Hierarchy->GetIndex(ControlKeys[KeyIndex]);
		if (ControlIndex != INDEX_NONE)
		{
			const FName GizmoName = Blueprint->Hierarchy->GetChecked<FRigControlElement>(ControlIndex)->Settings.GizmoName; 
			if(KeyIndex == 0)
			{
				Text = FText::FromName(GizmoName);
				FirstName = GizmoName;
			}
			else if(FirstName != GizmoName)
			{
				static const FString MultipleValues = TEXT("Multiple Values");
				Text = FText::FromString(MultipleValues);
				break;
			}
		}
	}
	return Text;
}

void SControlRigGizmoNameList::SetNameListText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/)
{
	const FScopedTransaction Transaction(NSLOCTEXT("ControlRigEditor", "ChangeGizmoName", "Change Gizmo Name"));

	for(int32 KeyIndex = 0; KeyIndex < ControlKeys.Num(); KeyIndex++)
	{
		const int32 ControlIndex = Blueprint->Hierarchy->GetIndex(ControlKeys[KeyIndex]);
		if (ControlIndex != INDEX_NONE)
		{
			const FName NewName = *NewTypeInValue.ToString();
			URigHierarchy* Hierarchy = Blueprint->Hierarchy;

			FRigControlElement* ControlElement = Hierarchy->Get<FRigControlElement>(ControlIndex);
			if ((ControlElement != nullptr) && (ControlElement->Settings.GizmoName != NewName))
			{
				Hierarchy->Modify();

				FRigControlSettings Settings = ControlElement->Settings;
				Settings.GizmoName = NewName;
				Hierarchy->SetControlSettings(ControlElement, Settings, true, true);
			}
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

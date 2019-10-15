// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Graph/SControlRigGraphPinNameList.h"
#include "Graph/SControlRigGraphPinNameListValueWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "DetailLayoutBuilder.h"

void SControlRigGraphPinNameList::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	this->OnGetNameListContent = InArgs._OnGetNameListContent;

	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget>	SControlRigGraphPinNameList::GetDefaultValueWidget()
{
	TSharedPtr<FString> InitialSelected;
	for (TSharedPtr<FString> Item : GetNameList())
	{
		if (Item->Equals(GetNameListText().ToString()))
		{
			InitialSelected = Item;
		}
	}

	return SNew(SBox)
		.MinDesiredWidth(150)
		.MaxDesiredWidth(400)
		[
			SAssignNew(NameListComboBox, SControlRigGraphPinNameListValueWidget)
				.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
				.OptionsSource(&GetNameList())
				.OnGenerateWidget(this, &SControlRigGraphPinNameList::MakeNameListItemWidget)
				.OnSelectionChanged(this, &SControlRigGraphPinNameList::OnNameListChanged)
				.OnComboBoxOpening(this, &SControlRigGraphPinNameList::OnNameListComboBox)
				.InitiallySelectedItem(InitialSelected)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &SControlRigGraphPinNameList::GetNameListText)
				]
		];
}

const TArray<TSharedPtr<FString>>& SControlRigGraphPinNameList::GetNameList() const
{
	if (OnGetNameListContent.IsBound())
	{
		return OnGetNameListContent.Execute();
	}
	return EmptyList;
}

FText SControlRigGraphPinNameList::GetNameListText() const
{
	return FText::FromString( GraphPinObj->GetDefaultAsString() );
}

void SControlRigGraphPinNameList::SetNameListText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/)
{
	if(!GraphPinObj->GetDefaultAsString().Equals(NewTypeInValue.ToString()))
	{
		const FScopedTransaction Transaction( NSLOCTEXT("GraphEditor", "ChangeBoneNameListPinValue", "Change Bone Name Pin Value" ) );
		GraphPinObj->Modify();
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, NewTypeInValue.ToString());
	}
}

TSharedRef<SWidget> SControlRigGraphPinNameList::MakeNameListItemWidget(TSharedPtr<FString> InItem)
{
	return 	SNew(STextBlock).Text(FText::FromString(*InItem));// .Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
}

void SControlRigGraphPinNameList::OnNameListChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		FString NewValue = *NewSelection.Get();
		SetNameListText(FText::FromString(NewValue), ETextCommit::OnEnter);
	}
}

void SControlRigGraphPinNameList::OnNameListComboBox()
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

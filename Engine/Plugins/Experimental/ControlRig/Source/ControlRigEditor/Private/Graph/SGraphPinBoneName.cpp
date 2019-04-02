// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Graph/SGraphPinBoneName.h"
#include "Graph/SGraphPinBoneNameValueWidget.h"
#include "Graph/ControlRigGraph.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "DetailLayoutBuilder.h"

void SGraphPinBoneName::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget>	SGraphPinBoneName::GetDefaultValueWidget()
{
	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GraphPinObj->GetOwningNode()->GetGraph());

	TSharedPtr<FString> InitialSelected;
	for (TSharedPtr<FString> Item : RigGraph->GetBoneNameList())
	{
		if (Item->Equals(GetBoneNameText().ToString()))
		{
			InitialSelected = Item;
		}
	}

	return SNew(SBox)
		.MinDesiredWidth(150)
		.MaxDesiredWidth(400)
		[
			SAssignNew(BoneNameComboBox, SGraphPinBoneNameValueWidget)
				.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
				.OptionsSource(&RigGraph->GetBoneNameList())
				.OnGenerateWidget(this, &SGraphPinBoneName::MakeBoneNameItemWidget)
				.OnSelectionChanged(this, &SGraphPinBoneName::OnBoneNameChanged)
				.OnComboBoxOpening(this, &SGraphPinBoneName::OnBoneNameComboBox)
				.InitiallySelectedItem(InitialSelected)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &SGraphPinBoneName::GetBoneNameText)
				]
		];
}

FText SGraphPinBoneName::GetBoneNameText() const
{
	return FText::FromString( GraphPinObj->GetDefaultAsString() );
}

void SGraphPinBoneName::SetBoneNameText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/)
{
	if(!GraphPinObj->GetDefaultAsString().Equals(NewTypeInValue.ToString()))
	{
		const FScopedTransaction Transaction( NSLOCTEXT("GraphEditor", "ChangeBoneNamePinValue", "Change Bone Name Pin Value" ) );
		GraphPinObj->Modify();
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, NewTypeInValue.ToString());
	}
}

TSharedRef<SWidget> SGraphPinBoneName::MakeBoneNameItemWidget(TSharedPtr<FString> InItem)
{
	return 	SNew(STextBlock).Text(FText::FromString(*InItem));// .Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
}

void SGraphPinBoneName::OnBoneNameChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		FString NewValue = *NewSelection.Get();
		SetBoneNameText(FText::FromString(NewValue), ETextCommit::OnEnter);
	}
}

void SGraphPinBoneName::OnBoneNameComboBox()
{
	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GraphPinObj->GetOwningNode()->GetGraph());
	TSharedPtr<FString> CurrentlySelected;
	for (TSharedPtr<FString> Item : RigGraph->GetBoneNameList())
	{
		if (Item->Equals(GetBoneNameText().ToString()))
		{
			CurrentlySelected = Item;
		}
	}
	BoneNameComboBox->SetSelectedItem(CurrentlySelected);
}

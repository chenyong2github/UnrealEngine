// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Graph/SGraphPinCurveName.h"
#include "Graph/SGraphPinBoneNameValueWidget.h"
#include "Graph/ControlRigGraph.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "DetailLayoutBuilder.h"

void SGraphPinCurveName::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget>	SGraphPinCurveName::GetDefaultValueWidget()
{
	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GraphPinObj->GetOwningNode()->GetGraph());

	TSharedPtr<FString> InitialSelected;
	for (TSharedPtr<FString> Item : RigGraph->GetCurveNameList())
	{
		if (Item->Equals(GetCurveNameText().ToString()))
		{
			InitialSelected = Item;
		}
	}

	return SNew(SBox)
		.MinDesiredWidth(150)
		.MaxDesiredWidth(400)
		[
			SAssignNew(CurveNameComboBox, SGraphPinBoneNameValueWidget)
				.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
				.OptionsSource(&RigGraph->GetCurveNameList())
				.OnGenerateWidget(this, &SGraphPinCurveName::MakeCurveNameItemWidget)
				.OnSelectionChanged(this, &SGraphPinCurveName::OnCurveNameChanged)
				.OnComboBoxOpening(this, &SGraphPinCurveName::OnCurveNameComboBox)
				.InitiallySelectedItem(InitialSelected)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &SGraphPinCurveName::GetCurveNameText)
				]
		];
}

FText SGraphPinCurveName::GetCurveNameText() const
{
	return FText::FromString( GraphPinObj->GetDefaultAsString() );
}

void SGraphPinCurveName::SetCurveNameText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/)
{
	if(!GraphPinObj->GetDefaultAsString().Equals(NewTypeInValue.ToString()))
	{
		const FScopedTransaction Transaction( NSLOCTEXT("GraphEditor", "ChangeCurveNamePinValue", "Change Bone Name Pin Value" ) );
		GraphPinObj->Modify();
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, NewTypeInValue.ToString());
	}
}

TSharedRef<SWidget> SGraphPinCurveName::MakeCurveNameItemWidget(TSharedPtr<FString> InItem)
{
	return 	SNew(STextBlock).Text(FText::FromString(*InItem));// .Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
}

void SGraphPinCurveName::OnCurveNameChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		FString NewValue = *NewSelection.Get();
		SetCurveNameText(FText::FromString(NewValue), ETextCommit::OnEnter);
	}
}

void SGraphPinCurveName::OnCurveNameComboBox()
{
	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GraphPinObj->GetOwningNode()->GetGraph());
	TSharedPtr<FString> CurrentlySelected;
	for (TSharedPtr<FString> Item : RigGraph->GetCurveNameList())
	{
		if (Item->Equals(GetCurveNameText().ToString()))
		{
			CurrentlySelected = Item;
		}
	}
	CurveNameComboBox->SetSelectedItem(CurrentlySelected);
}

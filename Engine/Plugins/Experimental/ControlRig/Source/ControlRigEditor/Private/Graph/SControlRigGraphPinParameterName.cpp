// Copyright Epic Games, Inc. All Rights Reserved.


#include "Graph/SControlRigGraphPinParameterName.h"
#include "Graph/ControlRigGraph.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "DetailLayoutBuilder.h"

void SControlRigGraphPinParameterName::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget>	SControlRigGraphPinParameterName::GetDefaultValueWidget()
{
	UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(GraphPinObj->GetOwningNode());
	URigVMGraph* Model = RigNode->GetModelNode()->GetGraph();

	TSharedPtr<FString> InitialSelected;

	TArray<TSharedPtr<FString>>& LocalParameterNames = GetParameterNames();
	for (TSharedPtr<FString> Item : LocalParameterNames)
	{
		if (Item->Equals(GetParameterNameText().ToString()))
		{
			InitialSelected = Item;
		}
	}

	return SNew(SBox)
		.MinDesiredWidth(150)
		.MaxDesiredWidth(400)
		[
			SAssignNew(NameComboBox, SControlRigGraphPinEditableNameValueWidget)
				.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
				.OptionsSource(&ParameterNames)
				.OnGenerateWidget(this, &SControlRigGraphPinParameterName::MakeParameterNameItemWidget)
				.OnSelectionChanged(this, &SControlRigGraphPinParameterName::OnParameterNameChanged)
				.OnComboBoxOpening(this, &SControlRigGraphPinParameterName::OnParameterNameComboBox)
				.InitiallySelectedItem(InitialSelected)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &SControlRigGraphPinParameterName::GetParameterNameText)
				]
		];
}

FText SControlRigGraphPinParameterName::GetParameterNameText() const
{
	return FText::FromString( GraphPinObj->GetDefaultAsString() );
}

void SControlRigGraphPinParameterName::SetParameterNameText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/)
{
	if(!GraphPinObj->GetDefaultAsString().Equals(NewTypeInValue.ToString()))
	{
		const FScopedTransaction Transaction( NSLOCTEXT("GraphEditor", "ChangeParameterNamePinValue", "Change Bone Name Pin Value" ) );
		GraphPinObj->Modify();
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, NewTypeInValue.ToString());
	}
}

TSharedRef<SWidget> SControlRigGraphPinParameterName::MakeParameterNameItemWidget(TSharedPtr<FString> InItem)
{
	return 	SNew(STextBlock).Text(FText::FromString(*InItem));// .Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
}

void SControlRigGraphPinParameterName::OnParameterNameChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		FString NewValue = *NewSelection.Get();
		SetParameterNameText(FText::FromString(NewValue), ETextCommit::OnEnter);
	}
}

void SControlRigGraphPinParameterName::OnParameterNameComboBox()
{
	TSharedPtr<FString> CurrentlySelected;
	TArray<TSharedPtr<FString>>& LocalParameterNames = GetParameterNames();
	for (TSharedPtr<FString> Item : LocalParameterNames)
	{
		if (Item->Equals(GetParameterNameText().ToString()))
		{
			CurrentlySelected = Item;
		}
	}

	NameComboBox->SetSelectedItem(CurrentlySelected);
}

TArray<TSharedPtr<FString>>& SControlRigGraphPinParameterName::GetParameterNames()
{
	if(UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(GraphPinObj->GetOwningNode()))
	{
		if(URigVMParameterNode* ModelNode = Cast<URigVMParameterNode>(RigNode->GetModelNode()))
		{
			ParameterNames.Reset();

			FRigVMGraphParameterDescription MyDescription = ModelNode->GetParameterDescription();

			TArray<FRigVMGraphParameterDescription> ParameterDescriptions = ModelNode->GetGraph()->GetParameterDescriptions();
			Algo::SortBy(ParameterDescriptions, &FRigVMGraphParameterDescription::Name, FNameLexicalLess());

			for (FRigVMGraphParameterDescription& ParameterDescription : ParameterDescriptions)
			{
				if (ParameterDescription.CPPType == MyDescription.CPPType && ParameterDescription.bIsInput == MyDescription.bIsInput)
				{
					ParameterNames.Add(MakeShared<FString>(ParameterDescription.Name.ToString()));
				}
			}
		}
	}

	return ParameterNames;
}

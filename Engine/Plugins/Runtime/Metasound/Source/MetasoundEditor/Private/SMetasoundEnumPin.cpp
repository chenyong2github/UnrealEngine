// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetasoundEnumPin.h"
#include "MetasoundEditorGraphNode.h"
#include "ScopedTransaction.h"

void SMetasoundEnumPin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget> SMetasoundEnumPin::GetDefaultValueWidget()
{
	//Get list of enum indexes
	TArray< TSharedPtr<int32> > ComboItems;
	GenerateComboBoxIndexes(ComboItems);

	//Create widget
	return SAssignNew(ComboBox, SPinComboBox)
		.ComboItemList(ComboItems)
		.VisibleText(this, &SMetasoundEnumPin::OnGetText)
		.OnSelectionChanged(this, &SMetasoundEnumPin::ComboBoxSelectionChanged)
		.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
		.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
		.OnGetDisplayName(this, &SMetasoundEnumPin::OnGetFriendlyName)
		.OnGetTooltip(this, &SMetasoundEnumPin::OnGetTooltip);
}

TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface> 
SMetasoundEnumPin::FindEnumInterfaceFromPin(UEdGraphPin* InPin) 
{
	auto MetasoundEditorNode = Cast<UMetasoundEditorGraphNode>(InPin->GetOwningNode());
	Metasound::Frontend::FNodeHandle NodeHandle = MetasoundEditorNode->GetNodeHandle();
	auto Inputs = NodeHandle->GetInputsWithVertexName(InPin->GetName());
	if (Inputs.Num() > 0)
	{
		FName DataType = Inputs[0]->GetDataType();
		return FMetasoundFrontendRegistryContainer::Get()->GetEnumInterfaceForDataType(DataType);
	}
	return nullptr;
}

FString SMetasoundEnumPin::OnGetText() const
{
	using namespace Metasound::Frontend;

	TSharedPtr<const IEnumDataTypeInterface> EnumInterface = FindEnumInterfaceFromPin(GraphPinObj);
	check(EnumInterface.IsValid());

	int32 SelectedValue = FCString::Atoi(*GraphPinObj->GetDefaultAsString());	// Enums are currently serialized as ints (the value of the enum).
	if (TOptional<IEnumDataTypeInterface::FGenericInt32Entry> Result = EnumInterface->FindByValue(SelectedValue))
	{
		return Result->DisplayName.ToString();
	}
	return TEXT("");
}

void SMetasoundEnumPin::GenerateComboBoxIndexes(TArray<TSharedPtr<int32>>& OutComboBoxIndexes)
{
	using namespace Metasound::Frontend;
	TSharedPtr<const IEnumDataTypeInterface> EnumInterface = FindEnumInterfaceFromPin(GraphPinObj);
	check(EnumInterface.IsValid());

	const TArray<IEnumDataTypeInterface::FGenericInt32Entry>& Entries = EnumInterface->GetAllEntries();
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		OutComboBoxIndexes.Add(MakeShared<int32>(i));
	}
}

void SMetasoundEnumPin::ComboBoxSelectionChanged(TSharedPtr<int32> NewSelection, ESelectInfo::Type SelectInfo)
{
	using namespace Metasound::Frontend;
	TSharedPtr<const IEnumDataTypeInterface> EnumInterface = FindEnumInterfaceFromPin(GraphPinObj);
	check(EnumInterface.IsValid());

	const TArray<IEnumDataTypeInterface::FGenericInt32Entry>& Entries = EnumInterface->GetAllEntries();

	if (NewSelection.IsValid() && Entries.IsValidIndex(*NewSelection))
	{
		int32 EnumValue = Entries[*NewSelection].Value;
		FString EnumValueString = FString::FromInt(EnumValue);
		if (GraphPinObj->GetDefaultAsString() != EnumValueString)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("GraphEditor", "ChangeEnumPinValue", "Change Enum Pin Value"));
			GraphPinObj->Modify();

			//Set new selection
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, EnumValueString);
		}
	}
}

FText SMetasoundEnumPin::OnGetFriendlyName(int32 EnumIndex)
{
	TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface> Interface = FindEnumInterfaceFromPin(GraphPinObj);
	check(Interface.IsValid());
	const auto& Entries = Interface->GetAllEntries();
	check(Entries.IsValidIndex(EnumIndex));
	return Entries[EnumIndex].DisplayName;
}

FText SMetasoundEnumPin::OnGetTooltip(int32 EnumIndex)
{
	TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface> Interface = FindEnumInterfaceFromPin(GraphPinObj);
	check(Interface.IsValid());
	const auto& Entries = Interface->GetAllEntries();
	check(Entries.IsValidIndex(EnumIndex));
	return Entries[EnumIndex].Tooltip;
}

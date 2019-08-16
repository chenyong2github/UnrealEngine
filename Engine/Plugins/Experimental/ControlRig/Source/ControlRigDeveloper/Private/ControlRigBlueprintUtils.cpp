// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigBlueprintUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Units/RigUnit.h"
#include "UObject/UObjectIterator.h"
#include "ControlRig.h"
#include "Graph/ControlRigGraphNode.h"
#include "ControlRigBlueprint.h"
#include "Kismet2/Kismet2NameValidators.h"

#define LOCTEXT_NAMESPACE "ControlRigBlueprintUtils"

FName FControlRigBlueprintUtils::GetNewUnitMemberName(UBlueprint* InBlueprint, const UStruct* InStructTemplate)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FString VariableBaseName = InStructTemplate->GetName();
	VariableBaseName.RemoveFromStart(TEXT("RigUnit_"));
	return FBlueprintEditorUtils::FindUniqueKismetName(InBlueprint, VariableBaseName);
}

FName FControlRigBlueprintUtils::AddUnitMember(UBlueprint* InBlueprint, const UStruct* InStructTemplate, const FName& InName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FName VarName = InName == NAME_None ? FControlRigBlueprintUtils::GetNewUnitMemberName(InBlueprint, InStructTemplate) : InName;

	UScriptStruct* ScriptStruct = FindObjectChecked<UScriptStruct>(ANY_PACKAGE, *InStructTemplate->GetName());
	UEdGraphSchema_K2 const* K2Schema = GetDefault<UEdGraphSchema_K2>();
	if(FBlueprintEditorUtils::AddMemberVariable(InBlueprint, VarName, FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, InStructTemplate->GetFName(), ScriptStruct, EPinContainerType::None, false, FEdGraphTerminalType())))
	{
		FBPVariableDescription& Variable = InBlueprint->NewVariables.Last();
		Variable.Category = LOCTEXT("UnitsCategory", "Units");

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);	

		return Variable.VarName;
	}

	return NAME_None;
}

FName FControlRigBlueprintUtils::GetNewPropertyMemberName(UBlueprint* InBlueprint, const FString& InVariableDesc)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	return FBlueprintEditorUtils::FindUniqueKismetName(InBlueprint, InVariableDesc);
}

FName FControlRigBlueprintUtils::AddPropertyMember(UBlueprint* InBlueprint, const FEdGraphPinType& InPinType, const FString& InVariableDesc)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if(FBlueprintEditorUtils::AddMemberVariable(InBlueprint, *InVariableDesc, InPinType))
	{
		return InBlueprint->NewVariables.Last().VarName;
	}
	return NAME_None;
}

FName FControlRigBlueprintUtils::ValidateName(UBlueprint* InBlueprint, const FString& InName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FString Name = InName;
	if (Name.StartsWith(TEXT("RigUnit_")))
	{
		Name = Name.RightChop(8);
	}

	TSharedPtr<FKismetNameValidator> NameValidator;
	NameValidator = MakeShareable(new FKismetNameValidator(InBlueprint));

	// Clean up BaseName to not contain any invalid characters, which will mean we can never find a legal name no matter how many numbers we add
	if (NameValidator->IsValid(Name) == EValidatorResult::ContainsInvalidCharacters)
	{
		for (TCHAR& TestChar : Name)
		{
			for (TCHAR BadChar : UE_BLUEPRINT_INVALID_NAME_CHARACTERS)
			{
				if (TestChar == BadChar)
				{
					TestChar = TEXT('_');
					break;
				}
			}
		}
	}

	if (UClass* ParentClass = InBlueprint->ParentClass)
	{
		if (UField* ExisingField = FindField<UField>(ParentClass, *Name))
		{
			Name = FString::Printf(TEXT("%s_%d"), *Name, 0);
		}
	}

	int32 Count = 0;
	FString BaseName = Name;
	while (NameValidator->IsValid(Name) != EValidatorResult::Ok)
	{
		// Calculate the number of digits in the number, adding 2 (1 extra to correctly count digits, another to account for the '_' that will be added to the name
		int32 CountLength = Count > 0 ? (int32)log((double)Count) + 2 : 2;

		// If the length of the final string will be too long, cut off the end so we can fit the number
		if (CountLength + BaseName.Len() > NameValidator->GetMaximumNameLength())
		{
			BaseName = BaseName.Left(NameValidator->GetMaximumNameLength() - CountLength);
		}
		Name = FString::Printf(TEXT("%s_%d"), *BaseName, Count);
		Count++;
	}

	return *Name;
}

UControlRigGraphNode* FControlRigBlueprintUtils::InstantiateGraphNodeForProperty(UEdGraph* InGraph, const FName& InPropertyName, const FVector2D& InLocation, const FEdGraphPinType& InPinType)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	check(InGraph);

	InGraph->Modify();

	UControlRigGraphNode* NewNode = NewObject<UControlRigGraphNode>(InGraph);
	NewNode->SetPropertyName(InPropertyName);
	NewNode->PinType = InPinType;

	InGraph->AddNode(NewNode, true);

	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	NewNode->NodePosX = InLocation.X;
	NewNode->NodePosY = InLocation.Y;

	NewNode->SetFlags(RF_Transactional);

	return NewNode;
}

UControlRigGraphNode* FControlRigBlueprintUtils::InstantiateGraphNodeForStructPath(UEdGraph* InGraph, const FName& InPropertyName, const FVector2D& InLocation, const FString& InStructPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	check(InGraph);

	InGraph->Modify();

	UControlRigGraphNode* NewNode = NewObject<UControlRigGraphNode>(InGraph);
	NewNode->SetPropertyName(InPropertyName);
	NewNode->StructPath = InStructPath;

	InGraph->AddNode(NewNode, true);

	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	NewNode->NodePosX = InLocation.X;
	NewNode->NodePosY = InLocation.Y;

	NewNode->SetFlags(RF_Transactional);

	return NewNode;
}

bool FControlRigBlueprintUtils::CanInstantiateGraphNodeForProperty(UEdGraph* InGraph, const FName& InPropertyName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	for(UEdGraphNode* Node : InGraph->Nodes)
	{
		if(UControlRigGraphNode* ControlRigGraphNode = Cast<UControlRigGraphNode>(Node))
		{
			if(ControlRigGraphNode->GetPropertyName() == InPropertyName)
			{
				return false;
			}
		}
	}

	return true;
}

void FControlRigBlueprintUtils::ForAllRigUnits(TFunction<void(UStruct*)> InFunction)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// Run over all unit types
	for(TObjectIterator<UStruct> StructIt; StructIt; ++StructIt)
	{
		if(StructIt->IsChildOf(FRigUnit::StaticStruct()) && !StructIt->HasMetaData(UControlRig::AbstractMetaName))
		{
			InFunction(*StructIt);
		}
	}
}

void FControlRigBlueprintUtils::HandleReconstructAllNodes(UBlueprint* InBlueprint)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if(InBlueprint->IsA<UControlRigBlueprint>())
	{
		// TArray<UControlRigGraphNode*> AllNodes;
		// FBlueprintEditorUtils::GetAllNodesOfClass(InBlueprint, AllNodes);

		// for(UControlRigGraphNode* Node : AllNodes)
		// {
		// 	Node->ReconstructNode();
		// }
	}
}

void FControlRigBlueprintUtils::HandleRefreshAllNodes(UBlueprint* InBlueprint)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if(InBlueprint->IsA<UControlRigBlueprint>())
	{
		TArray<UControlRigGraphNode*> AllNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(InBlueprint, AllNodes);

		for(UControlRigGraphNode* Node : AllNodes)
		{
			Node->ReconstructNode();
		}
	}
}

void FControlRigBlueprintUtils::HandleRenameVariableReferencesEvent(UBlueprint* InBlueprint, UClass* InVariableClass, const FName& InOldVarName, const FName& InNewVarName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if(UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(InBlueprint))
	{
		if (RigBlueprint->ModelController)
		{
			RigBlueprint->ModelController->RenameNode(InOldVarName, InNewVarName);
		}
	}
}

void FControlRigBlueprintUtils::RemoveMemberVariableIfNotUsed(UBlueprint* Blueprint, const FName VarName, UControlRigGraphNode* ToBeDeleted)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Blueprint->IsA<UControlRigBlueprint>())
	{
		bool bDeleteVariable = true;

		TArray<UControlRigGraphNode*> AllNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, AllNodes);

		for (UControlRigGraphNode* Node : AllNodes)
		{
			if (Node != ToBeDeleted && Node->GetPropertyName() == VarName)
			{
				bDeleteVariable = false;
				break;
			}
		}

		if (bDeleteVariable)
		{
			FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VarName);
		}
	}
}
#undef LOCTEXT_NAMESPACE
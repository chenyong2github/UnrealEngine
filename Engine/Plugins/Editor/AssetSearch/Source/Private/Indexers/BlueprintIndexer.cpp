// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintIndexer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Utility/IndexerUtilities.h"

#define LOCTEXT_NAMESPACE "FBlueprintIndexer"

PRAGMA_DISABLE_OPTIMIZATION

enum class EBlueprintIndexerVersion
{
	Empty = 0,
	Initial = 1,

	Current = Initial,
};

int32 FBlueprintIndexer::GetVersion() const
{
	return (int32)EBlueprintIndexerVersion::Current;
}

void FBlueprintIndexer::IndexAsset(const UObject* InAssetObject, FSearchSerializer& Serializer)
{
	const UBlueprint* BP = Cast<UBlueprint>(InAssetObject);
	check(BP);

	//UGameplayAbilityBlueprint

	if (UClass* GeneratedClass = BP->GeneratedClass)
	{
		if (UObject* CDO = GeneratedClass->GetDefaultObject())
		{
			Serializer.BeginIndexingObject(CDO, TEXT("Class Defaults"));
			FIndexerUtilities::IterateIndexableProperties(CDO, [&Serializer](const FProperty* Property, const FString& Value) {
				Serializer.IndexProperty(Property, Value);
			});
			Serializer.EndIndexingObject();
		}
	}

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{	
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			FText NodeType = LOCTEXT("Node", "Node");
			FText NodeText = Node->GetNodeTitle(ENodeTitleType::MenuTitle);
			Serializer.BeginIndexingObject(Node, NodeText);
			Serializer.IndexProperty(TEXT("Name"), NodeText);
			
			if (UK2Node_CallFunction* FunctionNode = Cast<UK2Node_CallFunction>(Node))
			{
				if (UFunction* TargetFunction = FunctionNode->GetTargetFunction())
				{
					Serializer.IndexProperty(TEXT("TargetFunction"), TargetFunction->GetName());
				}
			}
			if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
			{
				const FMemberReference& VariableReference = VariableNode->VariableReference;

				Serializer.IndexProperty(TEXT("MemberName"), VariableReference.GetMemberName().ToString());
				if (VariableReference.GetMemberGuid().IsValid())
				{
					Serializer.IndexProperty(TEXT("MemberGuid"), VariableReference.GetMemberGuid().ToString(EGuidFormats::Digits));
				}
				if (UClass* MemberParentClass = VariableReference.GetMemberParentClass())
				{
					Serializer.IndexProperty(TEXT("MemberParent"), MemberParentClass->GetPathName());
				}
				//Serializer.WriteValue(TEXT("bSelfContext"), VariableReference.IsSelfContext());
			}

			if (Node->GetAllPins().Num())
			{
				for (UEdGraphPin* Pin : Node->GetAllPins())
				{
					FText PinText = Pin->GetDisplayName();
					if (PinText.IsEmpty())
					{
						continue;
					}

					Serializer.IndexProperty(TEXT("Pin"), PinText);
				}
			}

			Serializer.EndIndexingObject();
		}
	}
}

#undef LOCTEXT_NAMESPACE

PRAGMA_ENABLE_OPTIMIZATION
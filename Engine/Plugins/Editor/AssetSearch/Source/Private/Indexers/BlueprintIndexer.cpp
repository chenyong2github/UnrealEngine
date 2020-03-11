// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintIndexer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Utility/IndexerUtilities.h"
#include "K2Node_BaseMCDelegate.h"
#include "Internationalization/Text.h"

#define LOCTEXT_NAMESPACE "FBlueprintIndexer"

PRAGMA_DISABLE_OPTIMIZATION

enum class EBlueprintIndexerVersion
{
	Empty,
	Initial,
	FixingPinsToSaveValues,

	// -----<new versions can be added above this line>-------------------------------------------------
	VersionPlusOne,
	LatestVersion = VersionPlusOne - 1
};

int32 FBlueprintIndexer::GetVersion() const
{
	return (int32)EBlueprintIndexerVersion::LatestVersion;
}

void FBlueprintIndexer::IndexAsset(const UObject* InAssetObject, FSearchSerializer& Serializer) const
{
	const UBlueprint* BP = Cast<UBlueprint>(InAssetObject);
	check(BP);

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
			const FText NodeText = Node->GetNodeTitle(ENodeTitleType::MenuTitle);
			Serializer.BeginIndexingObject(Node, NodeText);
			Serializer.IndexProperty(TEXT("Name"), NodeText);

			if (!Node->NodeComment.IsEmpty())
			{
				Serializer.IndexProperty(TEXT("Comment"), Node->NodeComment);
			}
			
			if (UK2Node_CallFunction* FunctionNode = Cast<UK2Node_CallFunction>(Node))
			{
				IndexMemberReference(Serializer, FunctionNode->FunctionReference, TEXT("Function"));
			}
			else if (UK2Node_BaseMCDelegate* DelegateNode = Cast<UK2Node_BaseMCDelegate>(Node))
			{
				IndexMemberReference(Serializer, DelegateNode->DelegateReference, TEXT("Delegate"));
			}
			else if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
			{
				IndexMemberReference(Serializer, VariableNode->VariableReference, TEXT("Variable"));
				//Serializer.WriteValue(TEXT("bSelfContext"), VariableReference.IsSelfContext());
			}

			if (Node->GetAllPins().Num())
			{
				for (UEdGraphPin* Pin : Node->GetAllPins())
				{
					const FText PinText = Pin->GetDisplayName();
					if (PinText.IsEmpty())
					{
						continue;
					}

					const FString PinValue = Pin->DefaultValue;
					if (PinValue.IsEmpty())
					{
						continue;
					}

					const FString PinLabel = TEXT("[Pin] ") + *FTextInspector::GetSourceString(PinText);
					Serializer.IndexProperty(PinLabel, PinValue);
				}
			}

			Serializer.EndIndexingObject();
		}
	}
}

void FBlueprintIndexer::IndexMemberReference(FSearchSerializer& Serializer, const FMemberReference& MemberReference, const FString& MemberType) const
{
	Serializer.IndexProperty(MemberType + TEXT("Name"), MemberReference.GetMemberName());

	if (MemberReference.GetMemberGuid().IsValid())
	{
		Serializer.IndexProperty(MemberType + TEXT("Guid"), MemberReference.GetMemberGuid().ToString(EGuidFormats::Digits));
	}

	if (UClass* MemberParentClass = MemberReference.GetMemberParentClass())
	{
		Serializer.IndexProperty(MemberType + TEXT("Parent"), MemberParentClass->GetPathName());
	}
}

#undef LOCTEXT_NAMESPACE

PRAGMA_ENABLE_OPTIMIZATION
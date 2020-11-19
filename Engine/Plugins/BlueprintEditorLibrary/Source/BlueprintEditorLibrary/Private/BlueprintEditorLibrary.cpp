// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintEditorLibrary.h"
#include "BlueprintEditorLibraryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphUtilities.h"
#include "BlueprintTypePromotion.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphUtilities.h"

DEFINE_LOG_CATEGORY(LogBlueprintEditorLib);

///////////////////////////////////////////////////////////
// InternalBlueprintEditorLibrary

namespace InternalBlueprintEditorLibrary
{
	/**
	* Replace the OldNode with the NewNode and reconnect it's pins. If the pins don't
	* exist on the NewNode, then orphan the connections.
	*
	* @param OldNode		The old node to replace
	* @param NewNode		The new node to put in the old node's place
	*/
	static void ReplaceOldNodeWithNew(UEdGraphNode* OldNode, UEdGraphNode* NewNode)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();\

		if (Schema && OldNode && NewNode)
		{
			TMap<FName, FName> OldToNewPinMap;
			for (UEdGraphPin* Pin : OldNode->Pins)
			{
				if (Pin->ParentPin != nullptr)
				{
					// ReplaceOldNodeWithNew() will take care of mapping split pins (as long as the parents are properly mapped)
					continue;
				}
				else if (Pin->PinName == UEdGraphSchema_K2::PN_Self)
				{
					// there's no analogous pin, signal that we're expecting this
					OldToNewPinMap.Add(Pin->PinName, NAME_None);
				}
				else
				{
					// The input pins follow the same naming scheme
					OldToNewPinMap.Add(Pin->PinName, Pin->PinName);
				}
			}
			
			Schema->ReplaceOldNodeWithNew(OldNode, NewNode, OldToNewPinMap);
		}
	}
};

///////////////////////////////////////////////////////////
// UBlueprintEditorLibrary

UBlueprintEditorLibrary::UBlueprintEditorLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void UBlueprintEditorLibrary::RenameVariableReferences(UBlueprint* Blueprint, const FName OldVarName, const FName NewVarName)
{
	if (!Blueprint)
	{
		return;
	}

	FBlueprintEditorUtils::RenameVariableReferences(Blueprint, Blueprint->GeneratedClass, OldVarName, NewVarName);
}

void UBlueprintEditorLibrary::ReplaceK2Nodes(UBlueprint* Blueprint, TSubclassOf<UEdGraphNode> OldNodeType, TSubclassOf<UEdGraphNode> NewNodeType)
{
	if (!Blueprint)
	{
		return;
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		Graph->Modify();

		// Iterate backwards through the nodes because we will be removing some
		for (int32 i = Graph->Nodes.Num() - 1; i >= 0; i--)
		{
			UEdGraphNode* OriginalNode = Graph->Nodes[i];
			if (OriginalNode && OriginalNode->IsA(OldNodeType))
			{
				// Spawn a new node of the new class
				UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, NewNodeType);
				NewNode->CreateNewGuid();
				NewNode->PostPlacedNewNode();
				NewNode->AllocateDefaultPins();
				NewNode->SetFlags(RF_Transactional);

				NewNode->NodePosX = OriginalNode->NodePosX;
				NewNode->NodePosY = OriginalNode->NodePosY;

				InternalBlueprintEditorLibrary::ReplaceOldNodeWithNew(OriginalNode, NewNode);
			}
		}
	}
}

UEdGraph* UBlueprintEditorLibrary::FindEventGraph(UBlueprint* Blueprint)
{
	return Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
}

UEdGraph* UBlueprintEditorLibrary::FindGraph(UBlueprint* Blueprint, FName GraphName)
{
	if (Blueprint)
	{
		for (UEdGraph* CurrentGraph : Blueprint->UbergraphPages)
		{
			if (CurrentGraph->GetFName() == GraphName)
			{
				return CurrentGraph;
			}
		}
	}

	return nullptr;
}

UK2Node_PromotableOperator* CreateOpNode(const FName OpName, UEdGraph* Graph, const int32 AdditionalPins)
{
	// The spawner will be null if type promo isn't enabled
	if (UBlueprintFunctionNodeSpawner* Spawner = FTypePromotion::GetOperatorSpawner(OpName))
	{
		// Spawn a new node!
		IBlueprintNodeBinder::FBindingSet Bindings;
		FVector2D SpawnLoc{};
		UK2Node_PromotableOperator* NewOpNode = Cast<UK2Node_PromotableOperator>(Spawner->Invoke(Graph, Bindings, SpawnLoc));

		// Add the necessary number of additional pins
		for (int32 i = 0; i < AdditionalPins; ++i)
		{
			NewOpNode->AddInputPin();
		}

		return NewOpNode;
	}

	return nullptr;
}

void UBlueprintEditorLibrary::UpgradeOperatorNodes(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return;
	}

	if (TypePromoDebug::IsTypePromoEnabled())
	{
		// Ensure that we have promotable operator node spanners available to us. 
		// They will be empty if the editor hasn't been opened or 
		if (FBlueprintActionDatabase* Actions = FBlueprintActionDatabase::TryGet())
		{
			Actions->RefreshAll();
		}
	}
	else
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Type Promotion is not enabled! Cannot upgrade operator nodes. Set 'BP.TypePromo.IsEnabled' to true and try again."));
		return;
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	Blueprint->Modify();

	for (UEdGraph* Graph : AllGraphs)
	{
		Graph->Modify();

		for (int32 i = Graph->Nodes.Num() - 1; i >= 0; --i)
		{
			// Not every function that we want to upgrade is a CommunicativeBinaryOpNode
			// Vector + Float is an example of this
			if (UK2Node_CallFunction* OldOpNode = Cast<UK2Node_CallFunction>(Graph->Nodes[i]))
			{
				UFunction* Func = OldOpNode->GetTargetFunction();

				// Don't bother with non-promotable functions or things that are already promotable operators
				if (!FTypePromotion::IsPromotableFunction(Func) || OldOpNode->IsA<UK2Node_PromotableOperator>())
				{
					continue;
				}

				FName OpName = FTypePromotion::GetOpNameFromFunction(Func);

				UK2Node_CommutativeAssociativeBinaryOperator* BinaryOpNode = Cast<UK2Node_CommutativeAssociativeBinaryOperator>(OldOpNode);

				// Spawn a new node!
				UK2Node_PromotableOperator* NewOpNode = CreateOpNode(
					OpName,
					OldOpNode->GetGraph(),
					BinaryOpNode ? BinaryOpNode->GetNumberOfAdditionalInputs() : 0
				);

				// If there is a node that is a communicative op node but is not promotable
				// then the node will be null
				if (!NewOpNode)
				{
					UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Failed to spawn new operator node!"));
					continue;
				}

				NewOpNode->NodePosX = OldOpNode->NodePosX;
				NewOpNode->NodePosY = OldOpNode->NodePosY;

				InternalBlueprintEditorLibrary::ReplaceOldNodeWithNew(OldOpNode, NewOpNode);
			}
		}
	}
}

void UBlueprintEditorLibrary::CompileBlueprint(UBlueprint* Blueprint)
{
	if (Blueprint)
	{
		// Skip saving this to avoid possible tautologies when saving and allow the user to manually save
		EBlueprintCompileOptions Flags = EBlueprintCompileOptions::SkipSave;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, Flags);
	}
}

UEdGraph* UBlueprintEditorLibrary::AddFunctionGraph(UBlueprint* Blueprint, const FString& FuncName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	Blueprint->Modify();
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, 
		FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, FuncName),
		UEdGraph::StaticClass(), 
		UEdGraphSchema_K2::StaticClass()
	);

	FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, NewGraph, /* bIsUserCreated = */ true, /* SignatureFromObject = */ nullptr);

	return NewGraph;
}

void UBlueprintEditorLibrary::RemoveFunctionGraph(UBlueprint* Blueprint, FName FuncName)
{
	if (!Blueprint)
	{
		return;
	}

	// Find the function graph of this name
	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == FuncName)
		{
			FunctionGraph = Graph;
			break;
		}
	}

	// Remove the function graph if we can
	if (FunctionGraph && FunctionGraph->bAllowDeletion)
	{
		Blueprint->Modify();
		FBlueprintEditorUtils::RemoveGraph(Blueprint, FunctionGraph, EGraphRemoveFlags::MarkTransient);
	}
	else
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Failed to remove function '%s' on blueprint '%s'!"), *FuncName.ToString(), *Blueprint->GetFriendlyName());
	}
}

void UBlueprintEditorLibrary::RemoveGraph(UBlueprint* Blueprint, UEdGraph* Graph)
{
	if (!Blueprint || !Graph)
	{
		return;
	}

	FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::MarkTransient);
}

void UBlueprintEditorLibrary::RenameGraph(UEdGraph* Graph, const FString& NewNameStr)
{
	if (!Graph)
	{
		return;
	}
		
	FBlueprintEditorUtils::RenameGraph(Graph, NewNameStr);
}

void UBlueprintEditorLibrary::AddComponent(UBlueprint* Blueprint, TSubclassOf<UActorComponent> ComponentClass)
{
	if (!Blueprint || !ComponentClass)
	{
		return;
	}
	//UClass* ActorClass = Blueprint->GeneratedClass;
	//TSubclassOf<UActorComponent> MatchingComponentClassForAsset = FComponentAssetBrokerage::GetPrimaryComponentForAsset(AssetClass);

	//AActor* CDO = Blueprint->GeneratedClass->
	// #TODO_BH Will need to discuss how to handle this correctly, there is a lot of
	// logic in SCS editor that I have questions about
}

UBlueprint* UBlueprintEditorLibrary::GetBlueprintAsset(UObject* Object)
{
	return Cast<UBlueprint>(Object);
}

void UBlueprintEditorLibrary::ReparentBlueprint(UBlueprint* Blueprint, UClass* NewParentClass)
{
	if (!Blueprint || !NewParentClass)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("Failed to reparent blueprint!"));
		return;
	}

	if (NewParentClass == Blueprint->ParentClass)
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("'%s' is already parented to class '%s'!"), *Blueprint->GetFriendlyName(), *NewParentClass->GetName());
		return;
	}

	// There could be possible data loss if reparenting outside the current class hierarchy
	if (!Blueprint->ParentClass || !NewParentClass->GetDefaultObject()->IsA(Blueprint->ParentClass))
	{
		UE_LOG(LogBlueprintEditorLib, Warning, TEXT("'%s' class heirarcy is changing, there could be possible data loss!"), *Blueprint->GetFriendlyName());
	}

	UClass* OriginalParentClass = Blueprint->ParentClass;
	Blueprint->ParentClass = NewParentClass;

	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	CompileBlueprint(Blueprint);
}
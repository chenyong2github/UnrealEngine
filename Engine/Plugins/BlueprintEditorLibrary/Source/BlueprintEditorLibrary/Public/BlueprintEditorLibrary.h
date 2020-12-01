// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/ActorComponent.h"
#include "EdGraph/EdGraphNode.h"
#include "BlueprintEditorLibrary.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBlueprintEditorLib, Warning, All);

UCLASS()
class UBlueprintEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()
public:

	/**
	* Replace any references of variables with the OldVarName to references of those with the NewVarName if possible
	*
	* @param Blueprint		Blueprint to replace the variable references on
	* @param OldVarName		The variable you want replaced
	* @param NewVarName		The new variable that will be used in the old one's place
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void RenameVariableReferences(UBlueprint* Blueprint, const FName OldVarName, const FName NewVarName);

	/**
	* Replace any existing k2node class with the new one, attempting to remap pins as we go
	*
	* @param Blueprint		Blueprint to replace node types on
	* @param OldNodeType	Old node type that will be removed
	* @param NewNodeType	New node type that will be put in the old node's place
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void ReplaceK2Nodes(UBlueprint* Blueprint, TSubclassOf<UEdGraphNode> OldNodeType, TSubclassOf<UEdGraphNode> NewNodeType);

	/**
	* Finds the event graph of the given blueprint. Null if it doesn't have one.
	*
	* @param Blueprint		Blueprint to search for the event graph on
	*
	* @return UEdGraph*		Event graph of the blueprint if it has one, null if it doesn't have one
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static UEdGraph* FindEventGraph(UBlueprint* Blueprint);

	/**
	* Finds the graph with the given name on the blueprint. Null if it doesn't have one. 
	*
	* @param Blueprint		Blueprint to search
	* @param GraphName		The name of the graph to search for 
	*
	* @return UEdGraph*		Pointer to the graph with the given name, null if not found
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static UEdGraph* FindGraph(UBlueprint* Blueprint, FName GraphName);

	/**
	* Replace any old operator nodes (float + float, vector + float, int + vector, etc)
	* with the newer Promotable Operator version of the node. Preserve any connections the
	* original node had to the newer version of the node. 
	*
	* @param Blueprint	Blueprint to upgrade
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void UpgradeOperatorNodes(UBlueprint* Blueprint);

	/**
	* Compiles the given blueprint. 
	*
	* @param Blueprint	Blueprint to compile
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void CompileBlueprint(UBlueprint* Blueprint);

	/**
	* Adds a function to the given blueprint
	*
	* @param Blueprint	The blueprint to remove the function from
	* @param FuncName	Name of the function to add
	*
	* @return UEdGraph*
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static UEdGraph* AddFunctionGraph(UBlueprint* Blueprint, const FString& FuncName);

	/** 
	* Deletes the function of the given name on this blueprint. Does NOT replace function call sites. 
	*
	* @param Blueprint		The blueprint to remove the function from
	* @param FuncName		The name of the function to remove
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void RemoveFunctionGraph(UBlueprint* Blueprint, FName FuncName);

	/** 
	* Removes the given graph from the blueprint if possible 
	* 
	* @param Blueprint	The blueprint the graph will be removed from
	* @param Graph		The graph to remove
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void RemoveGraph(UBlueprint* Blueprint, UEdGraph* Graph);

	/**
	* Attempts to rename the given graph with a new name
	*
	* @param Graph			The graph to rename
	* @param NewNameStr		The new name of the graph
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void RenameGraph(UEdGraph* Graph, const FString& NewNameStr);

	/**
	* Add an Actor Component to the given blueprint
	*
	* @param Blueprint			Blueprint to add the component to
	* @param ComponentClass		Type of component to add
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void AddComponent(UBlueprint* Blueprint, TSubclassOf<UActorComponent> ComponentClass);

	/**
	* Gets the UBlueprint version of the given object if possible.
	*
	* @param Object			The object we need to get the UBlueprint from
	*
	* @return UBlueprint*	The blueprint type of the given object, nullptr if the object is not a blueprint.
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static UBlueprint* GetBlueprintAsset(UObject* Object);

	/**
	* Attempts to reparent the given blueprint to the new chosen parent class. 
	*
	* @param Blueprint			Blueprint that you would like to reparent
	* @param NewParentClass		The new parent class to use
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void ReparentBlueprint(UBlueprint* Blueprint, UClass* NewParentClass);
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Templates/UniquePtr.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeBaseNodeContainer.generated.h"


/**
 * Interchange UInterchangeBaseNode graph is a format used to feed asset/scene import/reimport/export factories/writer.
 * This container hold a flat list of all nodes that have been translated from the source data.
 *
 * Translators are filling this container and the Import/Export managers are reading it to execute the import/export process
 */
 UCLASS(BlueprintType)
class INTERCHANGECORE_API UInterchangeBaseNodeContainer : public UObject
{
	GENERATED_BODY()
public:

	/**
	 * Add a node in the container, the node will be add into a TMap.
	 *
	 * @param Node - a pointer on the node you want to add
	 * @return: return the node unique ID of the added item. If the node already exist it will return the existing ID. Return InvalidNodeUID if the node cannot be added.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	FString AddNode(UInterchangeBaseNode* Node);

	/** Return true if the node unique ID exist in the container */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	bool IsNodeUIDValid(const FString& NodeUniqueID) const;

	/** Unordered iteration of the all nodes */
	void IterateNodes(TFunctionRef<void(const FString&, UInterchangeBaseNode*)> IterationLambda);

	/** Return all nodes that do not have any parent */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	void GetRoots(TArray<FString>& RootNodes);

	/** Return all nodes that are of the ClassNode type*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	void GetNodes(UClass* ClassNode, TArray<FString>& ClassNodes);

	/** Get an node pointer */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	UInterchangeBaseNode* GetNode(const FString& NodeUniqueID);

	/** Get an node pointer */
	const UInterchangeBaseNode* GetNode(const FString& NodeUniqueID) const;

	/** Set node ParentUID */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	bool SetNodeParentUID(const FString& NodeUniqueID, const FString& NewParentNodeUID);

	/** Get the node children count */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	int32 GetNodeChildrenCount(const FString& NodeUniqueID) const;

	/** Get all children UID */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	TArray<FString> GetNodeChildrenUIDs(const FString& NodeUniqueID) const;

	/** Get the node nth const children */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	UInterchangeBaseNode* GetNodeChildren(const FString& NodeUniqueID, int32 ChildIndex);

	/** Get the node nth const children. Const version */
	const UInterchangeBaseNode* GetNodeChildren(const FString& NodeUniqueID, int32 ChildIndex) const;

	/**
	 * This function serialize the node container and all node sub-objects point by it.
	 * Out of process translator like fbx will dump a file containing this data and the editor
	 * will read the file and regenerate the container from the saved data.
	 */
	void SerializeNodeContainerData(FArchive& Ar);

	/* Serialize the node container into the specified file.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	void SaveToFile(const FString& Filename);

	/* Serialize the node container from the specified file.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	void LoadFromFile(const FString& Filename);

private:

	UInterchangeBaseNode* GetNodeChildrenInternal(const FString& NodeUniqueID, int32 ChildIndex);

	/** Flat List of the nodes. Since the nodes are variable size, we store a pointer. */
	UPROPERTY()
	TMap<FString, TObjectPtr<UInterchangeBaseNode> > Nodes;
};

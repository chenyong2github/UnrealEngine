// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "BlueprintableTreeNode.generated.h"

/** Allows you to create a variable with a tree structure */
USTRUCT(BlueprintType, DisplayName = "Tree Hierarchy")
struct VCAMCORE_API FBlueprintableTreeHierarchy
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Tree")
	TObjectPtr<class UBlueprintableTreeNode> Node;
};

/**
 * Allows Blueprints to create generic tree like structures that are editable in the details panel.
 * Start by adding a struct property of type FBlueprintableTreeHierarchy as a Blueprint variable.
 *
 * Subclasses can define where its children come from.
 * The easiest implementation is UBlueprintableTreeNodeWithChildList, which stores its children in an instanced array.
 * More advanced ways could also be implemented, e.g. getting children from a data asset or another data source.
 * 
 * @see UBlueprintableTreeNodeWithChildList
 */
UCLASS(Abstract, EditInlineNew)
class VCAMCORE_API UBlueprintableTreeNode : public UObject
{
	GENERATED_BODY()
public:

	/** Gets this node's children, e.g. via a data asset reference. */
	virtual TArray<FBlueprintableTreeHierarchy> GetChildren() { unimplemented(); return {}; }

	UFUNCTION(BlueprintCallable, Category = "Tree")
	virtual UBlueprintableTreeNode* GetParent() const { unimplemented(); return nullptr; }

	DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(bool, FFilterTreeNode, UBlueprintableTreeNode*, Node);
	/**
	 * Goes through Children and returns all nodes which for which the supplied FilterDelegate returns true.
	 * @param FilterDelegate The filter to apply
	 * @param bRecursive Whether to include children's children.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tree")
	TArray<UBlueprintableTreeNode*> GetChildrenByFilter(const FFilterTreeNode& FilterDelegate, bool bRecursive = false);

	/** Gets all child nodes whose class is equal or descends from Class. */
	UFUNCTION(BlueprintCallable, Category = "Tree", meta = (DisplayName = "Get Children By Class", ScriptName = "GetChildrenByClass", DeterminesOutputType = "Class"))
	TArray<UBlueprintableTreeNode*> GetChildrenByClass(TSubclassOf<UBlueprintableTreeNode> Class, bool bRecursive = false);

	/**
	 * Goes through Children and calls the delegate on each of them.
	 * 
	 * @param FilterDelegate The filter to apply
	 * @param bRecursive Whether to include children's children.
	 */
	DECLARE_DYNAMIC_DELEGATE_OneParam(FProcessTreeNode, UBlueprintableTreeNode*, Node);
	UFUNCTION(BlueprintCallable, Category = "Tree")
	void ForEachChild(const FProcessTreeNode& ProcessDelegate, bool bRecursive = false);
	
	TArray<UBlueprintableTreeNode*> GetChildrenByFilter(TFunctionRef<bool(UBlueprintableTreeNode*)> FilterFunc, bool bRecursive = false);
	void ForEachChild(TFunctionRef<void(UBlueprintableTreeNode*)> Func, bool bRecursive = false);
};

/**
 * Allows Blueprints to create generic tree like structures that are editable in the details panel.
 * Start by adding a struct property of type FBlueprintableTreeHierarchy as a Blueprint variable.
 * 
 * Stores its children in an array.
 */
UCLASS(Abstract, Blueprintable)
class VCAMCORE_API UBlueprintableTreeNodeWithChildList : public UBlueprintableTreeNode
{
	GENERATED_BODY()
public:
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree")
	TArray<FBlueprintableTreeHierarchy> Children;

	//~ Begin UBlueprintableTreeNode Interface
	virtual UBlueprintableTreeNode* GetParent() const { return CastChecked<UBlueprintableTreeNode>(GetOuter()); }
	virtual TArray<FBlueprintableTreeHierarchy> GetChildren() override { return Children; }
	//~ End UBlueprintableTreeNode Interface
};

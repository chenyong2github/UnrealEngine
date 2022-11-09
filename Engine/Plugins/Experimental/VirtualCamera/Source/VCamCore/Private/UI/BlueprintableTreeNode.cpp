// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/BlueprintableTreeNode.h"

#include "Containers/Queue.h"

TArray<UBlueprintableTreeNode*> UBlueprintableTreeNode::GetChildrenByFilter(const FFilterTreeNode& FilterDelegate, bool bRecursive)
{
	if (!FilterDelegate.IsBound())
	{
		return {};
	}
	
	return GetChildrenByFilter([&FilterDelegate](UBlueprintableTreeNode* Node)
	{
		return FilterDelegate.Execute(Node);
	}, bRecursive);
}

TArray<UBlueprintableTreeNode*> UBlueprintableTreeNode::GetChildrenByClass(TSubclassOf<UBlueprintableTreeNode> Class, bool bRecursive)
{
	if (!Class)
	{
		return {};
	}
	
	return GetChildrenByFilter([Class](UBlueprintableTreeNode* Node)
	{
		return Node->GetClass()->IsChildOf(Class.Get());
	}, bRecursive);
}

void UBlueprintableTreeNode::ForEachChild(const FProcessTreeNode& ProcessDelegate, bool bOnlyFirstLevel)
{
	if (!ProcessDelegate.IsBound())
	{
		return;
	}
	
	return ForEachChild([&ProcessDelegate](UBlueprintableTreeNode* Node)
	{
		ProcessDelegate.Execute(Node);
	}, bOnlyFirstLevel);
}

TArray<UBlueprintableTreeNode*> UBlueprintableTreeNode::GetChildrenByFilter(TFunctionRef<bool(UBlueprintableTreeNode*)> FilterFunc, bool bRecursive)
{
	TArray<UBlueprintableTreeNode*> Result;
	ForEachChild([FilterFunc, &Result](UBlueprintableTreeNode* Node)
	{
		if (FilterFunc(Node))
		{
			Result.Add(Node);
		}
	}, bRecursive);
	return Result;
}

void UBlueprintableTreeNode::ForEachChild(TFunctionRef<void(UBlueprintableTreeNode*)> Func, bool bRecursive)
{
	TQueue<UBlueprintableTreeNode*> Queue;
	// In a tree it is technically redundant to check for cycles but GetChildren could return anything (breaking the tree property)
	TSet<UBlueprintableTreeNode*> EnqueuedNodes;

	auto EnqueueChildren = [&Queue, &EnqueuedNodes](UBlueprintableTreeNode* Node)
	{
		for (const FBlueprintableTreeHierarchy& Child : Node->GetChildren())
		{
			if (Child.Node && !EnqueuedNodes.Contains(Child.Node))
			{
				Queue.Enqueue(Child.Node);
				EnqueuedNodes.Add(Child.Node);
			}
		}
	};
	EnqueueChildren(this);

	UBlueprintableTreeNode* CurrentNode;
	while (Queue.Dequeue(CurrentNode))
	{
		// Blueprints could leave entries null
		if (!CurrentNode)
		{
			continue;
		}
		
		Func(CurrentNode);
		if (bRecursive)
		{
			EnqueueChildren(CurrentNode);
		}
	}
}

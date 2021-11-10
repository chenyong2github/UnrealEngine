// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Data type used to store and retrieve Blueprint namespace path components.
 * Note: Namespace identifier strings are expected to be of the form: "X.Y.Z"
 */
struct FBlueprintNamespacePathTree
{
public:
	/** Path tree node structure. */
	struct FNode
	{
		/** If TRUE, this node marks the end of an explicitly-added path string. Allows for "wildcard" paths which are inclusive of all subtrees. */
		bool bIsAddedPath = false;

		/** Maps path component names to any added child nodes (subtrees). */
		TMap<FName, TSharedPtr<struct FNode>> Children;

		/** Find or add the subtree associated with the given path component name as the key. */
		TSharedRef<FNode> FindOrAddChild(const FName& InKey)
		{
			if (const TSharedPtr<FNode>* NodePtr = Children.Find(InKey))
			{
				return NodePtr->ToSharedRef();
			}

			TSharedPtr<FNode> NewNode = MakeShared<FNode>();
			Children.Add(InKey, NewNode);

			return NewNode.ToSharedRef();
		}
	};

	FBlueprintNamespacePathTree()
	{
		// All added path identifier strings are rooted to this node.
		RootNode = MakeShared<FNode>();
	}

	TSharedRef<FNode> GetRootNode() const
	{
		check(RootNode.IsValid());
		return RootNode.ToSharedRef();
	}

	/**
	 * Attempts to locate an added path node that represents the given identifier string.
	 * 
	 * @param InPath					A Blueprint namespace path identifier string (e.g. "X.Y.Z").
	 * @param bMatchFirstInclusivePath	Whether to match on any prefix that represents an explicitly-added path (e.g. "X.Y.*").
	 * 
	 * @return A valid path node if the search was successful.
	 */
	TSharedPtr<FNode> FindPathNode(const FString& InPath, bool bMatchFirstInclusivePath = false) const
	{
		TSharedPtr<FNode> Node = GetRootNode();

		TArray<FString> PathSegments;
		InPath.ParseIntoArray(PathSegments, TEXT("."));
		for (const FString& PathSegment : PathSegments)
		{
			if (const TSharedPtr<FNode>* ChildNodePtr = Node->Children.Find(FName(*PathSegment)))
			{
				Node = *ChildNodePtr;

				if (bMatchFirstInclusivePath && Node->bIsAddedPath)
				{
					break;
				}
			}
			else
			{
				Node = nullptr;
				break;
			}
		}

		return Node;
	}

	/**
	 * Adds the given namespace identifier string as an explicitly-added path.
	 * 
	 * @param InPath	A Blueprint namespace path identifier string (e.g. "X.Y.Z").
	 */
	void AddPath(const FString& InPath)
	{
		TSharedRef<FNode> Node = GetRootNode();

		TArray<FString> PathSegments;
		InPath.ParseIntoArray(PathSegments, TEXT("."));
		for (const FString& PathSegment : PathSegments)
		{
			Node = Node->FindOrAddChild(FName(*PathSegment));
		}

		Node->bIsAddedPath = true;
	}

	/** Path node visitor function signature.
	 * 
	 * @param CurrentPath	Current path (represented as a stack of names).
	 * @param Node			A reference to the node at the current visitor level.
	 */
	typedef TFunctionRef<void(const TArray<FName>& /* CurrentPath */, TSharedRef<FBlueprintNamespacePathTree::FNode> /* Node */)> FNodeVisitorFunc;

	/**
	 * A utility method that will recursively visit all added nodes.
	 * 
	 * @param VisitorFunc	A function that will be called for each visited node.
	 */
	void ForeachNode(FNodeVisitorFunc VisitorFunc)
	{
		TArray<FName> CurrentPath;
		RecursiveNodeVisitor(GetRootNode(), CurrentPath, VisitorFunc);
	}

protected:
	/** Helper method for recursively visiting all nodes. */
	void RecursiveNodeVisitor(TSharedPtr<FBlueprintNamespacePathTree::FNode> Node, TArray<FName>& CurrentPath, FNodeVisitorFunc VisitorFunc)
	{
		for (auto ChildIt = Node->Children.CreateConstIterator(); ChildIt; ++ChildIt)
		{
			CurrentPath.Push(ChildIt.Key());

			TSharedPtr<FNode> ChildNode = ChildIt.Value();
			VisitorFunc(CurrentPath, ChildNode.ToSharedRef());

			RecursiveNodeVisitor(ChildNode, CurrentPath, VisitorFunc);
			CurrentPath.Pop();
		}
	}

private:
	TSharedPtr<FNode> RootNode;
};
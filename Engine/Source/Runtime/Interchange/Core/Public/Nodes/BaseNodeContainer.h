// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "Nodes/BaseNode.h"

//Interchange namespace
namespace Interchange
{

/**
 * Interchange FBaseNode graph is a format used to feed asset/scene import/reimport/export factories/writer.
 * This container hold a flat list of all nodes that have been translated from the source data.
 *
 * Translators are filling this container and the Import/Export managers are reading it to execute the import/export process
 */
class INTERCHANGECORE_API FBaseNodeContainer
{
public:
	//To avoid issue with the TMap<FNodeUniqueID, TUniquePtr<FBaseNode> > Nodes
	//We have to delete the copy constructor and the = operator to avoid CopyToDelete to be instantiate which will implement a copy of the TMap.
	//The CopyToEmpty is instantiate when we add the INTERCHANGECORE_API to the class
	FBaseNodeContainer(const FBaseNodeContainer&) = delete;
	FBaseNodeContainer& operator=(const FBaseNodeContainer&) = delete;

	FBaseNodeContainer() = default;

	/**
	 * Add a node in the container, the node will be set into a TUniquePtr, which mean the container will have the memory ownership of the pass pointer.
	 * You can get a ref of the added node by using GetNode function.
	 *
	 * @param Node - a pointer on the node you want to add
	 * @return: return the node unique ID of the added item, return InvalidNodeUID if the node is already there or cannot be added.
	 *
	 * @note The node memory will be manage by the container after the add, the destructor of the container will free correctly all nodes
	 *
	 */
	FNodeUniqueID AddNode(TUniquePtr<FBaseNode> Node)
	{
		if (!Node.IsValid())
		{
			return FBaseNode::InvalidNodeUID();
		}

		FNodeUniqueID NodeUniqueID = Node->GetUniqueID();
		if (NodeUniqueID == FBaseNode::InvalidNodeUID())
		{
			return FBaseNode::InvalidNodeUID();
		}
		
		//Cannot add an node with the same IDs
		if (Nodes.Contains(NodeUniqueID))
		{
			return FBaseNode::InvalidNodeUID();
		}
		//Create a unique node pointer
		Nodes.Add(NodeUniqueID, MoveTemp(Node));
		return NodeUniqueID;
	}

	/** Return true if the node unique ID exist in the container */
	bool IsNodeUIDValid(FNodeUniqueID NodeUniqueID) const
	{
		if (NodeUniqueID == FBaseNode::InvalidNodeUID())
		{
			return false;
		}
		return Nodes.Contains(NodeUniqueID);
	}

	/** Unordered iteration of the all nodes */
	void IterateNodes(TFunctionRef<void(const FNodeUniqueID, const FBaseNode*)> IterationLambda)
	{
		for (TPair<FNodeUniqueID, TUniquePtr<FBaseNode>>& NodeKeyValue : Nodes)
		{
			IterationLambda(NodeKeyValue.Key, NodeKeyValue.Value.Get());
		}
	}

	/** Return all nodes that do not have any parent */
	void GetRoots(TArray<FNodeUniqueID>& RootNodes)
	{
		for (TPair<FNodeUniqueID, TUniquePtr<FBaseNode>>& NodeKeyValue : Nodes)
		{
			if (NodeKeyValue.Value->GetParentUID() == FBaseNode::InvalidNodeUID())
			{
				RootNodes.Add(NodeKeyValue.Key);
			}
		}
	}

	/** Get an node pointer */
	FBaseNode* GetNode(FNodeUniqueID NodeUniqueID)
	{
		if (NodeUniqueID == FBaseNode::InvalidNodeUID())
		{
			return nullptr;
		}
		if (!Nodes.Contains(NodeUniqueID))
		{
			return nullptr;
		}
		if(TUniquePtr<FBaseNode>& Node = Nodes.FindChecked(NodeUniqueID))
		{
			return Node.Get();
		}
		return nullptr;
	}

	/** Get an node pointer */
	const FBaseNode* GetNode(FNodeUniqueID NodeUniqueID) const
	{
		return const_cast<FBaseNodeContainer*>(this)->GetNode(NodeUniqueID);
	}

	/** Get an node reference. Assert if there is an error */
	FBaseNode& GetNodeChecked(FNodeUniqueID NodeUniqueID)
	{
		return GetNodeCheckedInternal(NodeUniqueID);
	}

	/** Get an node reference. Assert if there is an error */
	const FBaseNode& GetNodeCheck(FNodeUniqueID NodeUniqueID) const
	{
		return const_cast<FBaseNodeContainer*>(this)->GetNodeCheckedInternal(NodeUniqueID);
	}

	/** Set node ParentUID */
	bool SetNodeParentUID(FNodeUniqueID NodeUniqueID, FNodeUniqueID NewParentNodeUID)
	{
		if (!Nodes.Contains(NodeUniqueID))
		{
			return false;
		}
		if (!Nodes.Contains(NewParentNodeUID))
		{
			return false;
		}
		TUniquePtr<FBaseNode>& Node = Nodes.FindChecked(NodeUniqueID);
		check(Node.IsValid());
		Node->SetParentUID(NewParentNodeUID);
		return true;
	}

	/** Get the node children count */
	int32 GetNodeChildrenCount(FNodeUniqueID NodeUniqueID) const
	{
		TArray<FNodeUniqueID> ChildrenUIDs = GetNodeChildrenUIDs(NodeUniqueID);
		return ChildrenUIDs.Num();
	}

	/** Get all children UID */
	TArray<FNodeUniqueID> GetNodeChildrenUIDs(FNodeUniqueID NodeUniqueID) const
	{
		TArray<FNodeUniqueID> OutChildrenUIDs;
		for (const TPair<FNodeUniqueID, TUniquePtr<FBaseNode>>& NodeKeyValue : Nodes)
		{
			if (NodeKeyValue.Value->GetParentUID() == NodeUniqueID)
			{
				OutChildrenUIDs.Add(NodeKeyValue.Key);
			}
		}
		return OutChildrenUIDs;
	}

	/** Get the node nth const children */
	FBaseNode* GetNodeChildren(FNodeUniqueID NodeUniqueID, int32 ChildIndex)
	{
		return GetNodeChildrenInternal(NodeUniqueID, ChildIndex);
	}

	/** Get the node nth const children. Const version */
	const FBaseNode* GetNodeChildren(FNodeUniqueID NodeUniqueID, int32 ChildIndex) const
	{
		return const_cast<FBaseNodeContainer*>(this)->GetNodeChildrenInternal(NodeUniqueID, ChildIndex);
	}

	/** Get the node nth const children. Assert if there is an error */
	FBaseNode& GetNodeChildrenChecked(FNodeUniqueID NodeUniqueID, int32 ChildIndex)
	{
		TArray<FNodeUniqueID> ChildrenUIDs = GetNodeChildrenUIDs(NodeUniqueID);
		check (!ChildrenUIDs.IsValidIndex(ChildIndex));
		TUniquePtr<FBaseNode>& Node = Nodes.FindChecked(ChildrenUIDs[ChildIndex]);
		check(Node.IsValid())
		return *Node.Get();
	}

	/** Get the node nth const children. Const version. Assert if there is an error */
	const FBaseNode& GetNodeChildrenChecked(FNodeUniqueID NodeUniqueID, int32 ChildIndex) const
	{
		return const_cast<FBaseNodeContainer*>(this)->GetNodeChildrenChecked(NodeUniqueID, ChildIndex);
	}

private:

	FBaseNode& GetNodeCheckedInternal(FNodeUniqueID NodeUniqueID)
	{
		check(NodeUniqueID != FBaseNode::InvalidNodeUID());
		TUniquePtr<FBaseNode>& Node = Nodes.FindChecked(NodeUniqueID);
		check(Node.IsValid());
		return *Node.Get();
	}

	FBaseNode* GetNodeChildrenInternal(FNodeUniqueID NodeUniqueID, int32 ChildIndex)
	{
		TArray<FNodeUniqueID> ChildrenUIDs = GetNodeChildrenUIDs(NodeUniqueID);
		if (!ChildrenUIDs.IsValidIndex(ChildIndex))
		{
			return nullptr;
		}

		if (Nodes.Contains(ChildrenUIDs[ChildIndex]))
		{
			TUniquePtr<FBaseNode>& Node = Nodes.FindChecked(ChildrenUIDs[ChildIndex]);
			return Node.Get();
		}

		return nullptr;
	}

	/** Flat List of the nodes. Since the nodes are variable size, we store a pointer. */
	TMap<FNodeUniqueID, TUniquePtr<FBaseNode> > Nodes;
};

}

// Copyright Epic Games, Inc. All Rights Reserved.
#include "Nodes/InterchangeBaseNodeContainer.h"

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/LargeMemoryReader.h"
#include "Templates/UniquePtr.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectIterator.h"

FString UInterchangeBaseNodeContainer::AddNode(UInterchangeBaseNode* Node)
{
	if (!Node)
	{
		return UInterchangeBaseNode::InvalidNodeUID();
	}
	FString NodeUniqueID = Node->GetUniqueID();
	if (NodeUniqueID == UInterchangeBaseNode::InvalidNodeUID())
	{
		return UInterchangeBaseNode::InvalidNodeUID();
	}
		
	//Cannot add an node with the same IDs
	if (Nodes.Contains(NodeUniqueID))
	{
		return NodeUniqueID;
	}

	if (Node->GetDisplayLabel().IsEmpty())
	{
		//Replace None by Null, since None name will be interpret like NAME_None which will not work with UObject creation
		//UObject Creation will name it ClassName_X instead of None
		//TODO Log an warning to the user
		Node->SetDisplayLabel(FString(TEXT("Null")));
	}

	//Copy the node
	Nodes.Add(NodeUniqueID, Node);
	return NodeUniqueID;
}

bool UInterchangeBaseNodeContainer::IsNodeUIDValid(const FString& NodeUniqueID) const
{
	if (NodeUniqueID == UInterchangeBaseNode::InvalidNodeUID())
	{
		return false;
	}
	return Nodes.Contains(NodeUniqueID);
}

void UInterchangeBaseNodeContainer::IterateNodes(TFunctionRef<void(const FString&, UInterchangeBaseNode*)> IterationLambda)
{
	for (auto& NodeKeyValue : Nodes)
	{
		IterationLambda(NodeKeyValue.Key, NodeKeyValue.Value);
	}
}

void UInterchangeBaseNodeContainer::GetRoots(TArray<FString>& RootNodes)
{
	for (auto& NodeKeyValue : Nodes)
	{
		if (NodeKeyValue.Value->GetParentUID() == UInterchangeBaseNode::InvalidNodeUID())
		{
			RootNodes.Add(NodeKeyValue.Key);
		}
	}
}

void UInterchangeBaseNodeContainer::GetNodes(UClass* ClassNode, TArray<FString>& ClassNodes)
{
	IterateNodes([&ClassNode, &ClassNodes](const FString& NodeUID, UInterchangeBaseNode* Node)
	{
		if(Node->GetClass()->IsChildOf(ClassNode))
		{
			ClassNodes.Add(Node->GetUniqueID());
		}
	});
}

UInterchangeBaseNode* UInterchangeBaseNodeContainer::GetNode(const FString& NodeUniqueID)
{
	if (NodeUniqueID == UInterchangeBaseNode::InvalidNodeUID())
	{
		return nullptr;
	}
	if (!Nodes.Contains(NodeUniqueID))
	{
		return nullptr;
	}
	UInterchangeBaseNode* Node = Nodes.FindChecked(NodeUniqueID);
	return Node;
}

const UInterchangeBaseNode* UInterchangeBaseNodeContainer::GetNode(const FString& NodeUniqueID) const
{
	return const_cast<UInterchangeBaseNodeContainer*>(this)->GetNode(NodeUniqueID);
}

bool UInterchangeBaseNodeContainer::SetNodeParentUID(const FString& NodeUniqueID, const FString& NewParentNodeUID)
{
	if (!Nodes.Contains(NodeUniqueID))
	{
		return false;
	}
	if (!Nodes.Contains(NewParentNodeUID))
	{
		return false;
	}
	UInterchangeBaseNode* Node = Nodes.FindChecked(NodeUniqueID);
	Node->SetParentUID(NewParentNodeUID);
	return true;
}

int32 UInterchangeBaseNodeContainer::GetNodeChildrenCount(const FString& NodeUniqueID) const
{
	TArray<FString> ChildrenUIDs = GetNodeChildrenUIDs(NodeUniqueID);
	return ChildrenUIDs.Num();
}

TArray<FString> UInterchangeBaseNodeContainer::GetNodeChildrenUIDs(const FString& NodeUniqueID) const
{
	TArray<FString> OutChildrenUIDs;
	for (const auto& NodeKeyValue : Nodes)
	{
		if (NodeKeyValue.Value->GetParentUID() == NodeUniqueID)
		{
			OutChildrenUIDs.Add(NodeKeyValue.Key);
		}
	}
	return OutChildrenUIDs;
}

UInterchangeBaseNode* UInterchangeBaseNodeContainer::GetNodeChildren(const FString& NodeUniqueID, int32 ChildIndex)
{
	return GetNodeChildrenInternal(NodeUniqueID, ChildIndex);
}

const UInterchangeBaseNode* UInterchangeBaseNodeContainer::GetNodeChildren(const FString& NodeUniqueID, int32 ChildIndex) const
{
	return const_cast<UInterchangeBaseNodeContainer*>(this)->GetNodeChildrenInternal(NodeUniqueID, ChildIndex);
}

void UInterchangeBaseNodeContainer::SerializeNodeContainerData(FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		Nodes.Reset();
	}
	int32 NodeCount = Nodes.Num();
	Ar << NodeCount;

	if(Ar.IsSaving())
	{
		//The node name is not serialize since its an attribute inside the node that will be serialize by the node himself
		auto SerializeNodePair = [&Ar](UInterchangeBaseNode* BaseNode)
		{
			FString ClassFullName = BaseNode->GetClass()->GetFullName();
			Ar << ClassFullName;
			BaseNode->Serialize(Ar);
		};

		for (auto NodePair : Nodes)
		{
			SerializeNodePair(NodePair.Value);
		}
	}
	else if(Ar.IsLoading())
	{
		//Find all the potential node class
		TMap<FString, UClass*> ClassPerName;
		for (FThreadSafeObjectIterator It(UClass::StaticClass()); It; ++It)
		{
			UClass* Class = Cast<UClass>(*It);
			if (Class->IsChildOf(UInterchangeBaseNode::StaticClass()))
			{
				ClassPerName.Add(Class->GetFullName(), Class);
			}
		}

		for (int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
		{
			FString ClassFullName;
			Ar << ClassFullName;
			//This cannot fail to make sure we have a healty serialization
			if (!ensure(ClassPerName.Contains(ClassFullName)))
			{
				//We did not successfully serialize the content of the file into the node container
				return;
			}
			UClass* ToCreateClass = ClassPerName.FindChecked(ClassFullName);
			//Create a UInterchangeBaseNode with the proper class
			UInterchangeBaseNode* BaseNode = NewObject<UInterchangeBaseNode>(this, ToCreateClass);
			BaseNode->Serialize(Ar);
			AddNode(BaseNode);
		}
	}
}

void UInterchangeBaseNodeContainer::SaveToFile(const FString& Filename)
{
	FLargeMemoryWriter Ar;
	SerializeNodeContainerData(Ar);
	uint8* ArchiveData = Ar.GetData();
	int64 ArchiveSize = Ar.TotalSize();
	TArray64<uint8> Buffer(ArchiveData, ArchiveSize);
	FFileHelper::SaveArrayToFile(Buffer, *Filename);
}

void UInterchangeBaseNodeContainer::LoadFromFile(const FString& Filename)
{
	//All sub object should be gone with the reset
	Nodes.Reset();
	TArray64<uint8> Buffer;
	FFileHelper::LoadFileToArray(Buffer, *Filename);
	uint8* FileData = Buffer.GetData();
	int64 FileDataSize = Buffer.Num();
	if (FileDataSize < 1)
	{
		//Nothing to load from this file
		return;
	}
	//Buffer keep the ownership of the data, the large memory reader is use to serialize the TMap
	FLargeMemoryReader Ar(FileData, FileDataSize);
	SerializeNodeContainerData(Ar);
}

UInterchangeBaseNode* UInterchangeBaseNodeContainer::GetNodeChildrenInternal(const FString& NodeUniqueID, int32 ChildIndex)
{
	TArray<FString> ChildrenUIDs = GetNodeChildrenUIDs(NodeUniqueID);
	if (!ChildrenUIDs.IsValidIndex(ChildIndex))
	{
		return nullptr;
	}

	if (Nodes.Contains(ChildrenUIDs[ChildIndex]))
	{
		UInterchangeBaseNode* Node = Nodes.FindChecked(ChildrenUIDs[ChildIndex]);
		return Node;
	}

	return nullptr;
}

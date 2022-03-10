// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/SoftObjectPtr.h"

struct GRAPHEDITOR_API FEdGraphNodeHandle
{
public:

	FORCEINLINE FEdGraphNodeHandle(const UEdGraphNode* InNode)
		: Graph(InNode->GetGraph())
		, NodeName(InNode->GetFName())
	{}

	FORCEINLINE FEdGraphNodeHandle(const FEdGraphNodeHandle& InOther)
		: Graph(InOther.Graph)
		, NodeName(InOther.NodeName)
	{}

	friend FORCEINLINE uint32 GetTypeHash(const FEdGraphNodeHandle& InHandle)
	{
		return HashCombine(GetTypeHash(InHandle.Graph.ToSoftObjectPath()), GetTypeHash(InHandle.NodeName));
	}

	FORCEINLINE bool operator ==(const FEdGraphNodeHandle& InOther) const
	{
		return Graph.GetUniqueID() == InOther.Graph.GetUniqueID() &&
			NodeName.IsEqual(InOther.NodeName, ENameCase::CaseSensitive, true);
	}

	FORCEINLINE const UEdGraph* GetGraph() const { return Graph.Get(); }

	FORCEINLINE UEdGraphNode* GetNode() const
	{
		if(const UEdGraph* EdGraph = GetGraph())
		{
			const TObjectPtr<UEdGraphNode>* Node = EdGraph->Nodes.FindByPredicate([this](TObjectPtr<UEdGraphNode> Node) -> bool
			{
				return Node->GetFName() == NodeName;
			});
			if(Node)
			{
				return Node->Get();
			}
		}
		return nullptr;
	}

private:
	TSoftObjectPtr<UEdGraph> Graph;
	FName NodeName;
};

struct GRAPHEDITOR_API FEdGraphPinHandle : FEdGraphNodeHandle
{
public:

	FORCEINLINE FEdGraphPinHandle(const UEdGraphPin* InPin)
		: FEdGraphNodeHandle(InPin->GetOwningNode())
		, PinName(InPin->GetFName())
	{}
		
	FORCEINLINE FEdGraphPinHandle(const FEdGraphPinHandle& InOther)
		: FEdGraphNodeHandle(InOther)
		, PinName(InOther.PinName)
	{}

	friend FORCEINLINE uint32 GetTypeHash(const FEdGraphPinHandle& InHandle)
	{
		return HashCombine(GetTypeHash((FEdGraphNodeHandle)InHandle), GetTypeHash(InHandle.PinName));
	}

	FORCEINLINE bool operator ==(const FEdGraphPinHandle& InOther) const
	{
		return FEdGraphNodeHandle::operator==(InOther) &&
			PinName.IsEqual(InOther.PinName, ENameCase::CaseSensitive, true);
	}
	
	FORCEINLINE UEdGraphPin* GetPin() const
	{
		if(UEdGraphNode* EdNode = GetNode())
		{
			const UEdGraphPin*const* Pin = EdNode->Pins.FindByPredicate([this](UEdGraphPin* Pin) -> bool
			{
				return Pin->GetFName() == PinName;
			});
			if(Pin)
			{
				return (UEdGraphPin*)*Pin;
			}
		}
		return nullptr;
	}

private:
	FName PinName;
};


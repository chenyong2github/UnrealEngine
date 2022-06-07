// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosArchive.h"
#include "Dataflow/DataflowNode.h"
#include "Serialization/Archive.h"

namespace Dataflow
{
	class FConnection;

	struct FLink {
		FGuid InputNode;
		FGuid Input;
		FGuid OutputNode;
		FGuid Output;

		FLink() {}

		FLink(FGuid InInputNode, FGuid InInput, FGuid InOutputNode, FGuid InOutput)
			: InputNode(InInputNode), Input(InInput)
			, OutputNode(InOutputNode), Output(InOutput) {}

		FLink(const FLink& Other)
			: InputNode(Other.InputNode), Input(Other.Input)
			, OutputNode(Other.OutputNode), Output(Other.Output) {}

		bool operator==(const FLink& Other) const
		{
			return Equals(Other);
		}

		bool Equals(const FLink& Other) const
		{
			return Input == Other.Input && InputNode == Other.InputNode
				&& Output == Other.Output && OutputNode == Other.OutputNode;
		}
	};


	//
	//
	//
	class DATAFLOWCORE_API FGraph
	{

		FGuid  Guid;
		TArray< TSharedPtr<FNode> > Nodes;
		TArray< FLink > Connections;
		TSet< FName > DisabledNodes;
	public:
		FGraph(FGuid InGuid = FGuid::NewGuid());
		virtual ~FGraph() {}

		const TArray< TSharedPtr<FNode> >& GetNodes() const {return Nodes;}
		TArray< TSharedPtr<FNode> >& GetNodes() { return Nodes; }
		int NumNodes() { return Nodes.Num(); }

		template<class T> TSharedPtr<T> AddNode(T* InNode)
		{
			TSharedPtr<T> NewNode(InNode);
			Nodes.AddUnique(NewNode);
			return NewNode;
		}

		TSharedPtr<FNode> FindBaseNode(FGuid InGuid)
		{
			for (TSharedPtr<FNode> Node : Nodes)
			{
				if (Node->GetGuid() == InGuid)
				{
					return Node;
				}
			}
			return TSharedPtr<FNode>(nullptr);
		}


		void RemoveNode(TSharedPtr<FNode> Node);

		void ClearConnections(FConnection*);
		void Connect(FConnection* Input, FConnection* Output);
		void Disconnect(FConnection* Input, FConnection* Output);

		virtual void Serialize(FArchive& Ar);
		const TSet<FName>& GetDisabledNodes() const { return DisabledNodes; }

	};
}

FORCEINLINE FArchive& operator<<(FArchive& Ar, Dataflow::FLink& Value)
{
	Ar << Value.InputNode << Value.OutputNode << Value.Input << Value.Output;
	return Ar;
}

FORCEINLINE FArchive& operator<<(Chaos::FChaosArchive& Ar, Dataflow::FLink& Value)
{
	Ar << Value.InputNode << Value.OutputNode << Value.Input << Value.Output;
	return Ar;
}

FORCEINLINE FArchive& operator<<(FArchive& Ar, Dataflow::FGraph& Value)
{
	Value.Serialize(Ar);
	return Ar;
}

FORCEINLINE FArchive& operator<<(Chaos::FChaosArchive& Ar, Dataflow::FGraph& Value)
{
	Value.Serialize(Ar);
	return Ar;
}





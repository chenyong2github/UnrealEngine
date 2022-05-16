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
		FGuid Input;
		FGuid Output;

		FLink() {}

		FLink(FGuid InInput, FGuid InOutput)
			: Input(InInput), Output(InOutput) {}

		FLink(const FLink& Other)
			: Input(Other.Input), Output(Other.Output) {}

		bool operator==(const FLink& Other) const
		{
			return Equals(Other);
		}

		bool Equals(const FLink& Other) const
		{
			return Input == Other.Input
				&& Output == Other.Output;
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

		template<class T> T* FindNode(FName InName)
		{
			for (TSharedPtr<FNode> Node : Nodes)
			{
				if (Node->GetName().ToString().Equals(InName.ToString()))
				{
					if (Node->GetType().ToString().Equals(T::Type.ToString()))
					{
						return reinterpret_cast<T*>(Node.Get()); // @todo(eg) : Can we do better here?
					}
				}
			}
			return nullptr;
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

	};
}

FORCEINLINE FArchive& operator<<(FArchive& Ar, Dataflow::FLink& Value)
{
	Ar << Value.Input << Value.Output;
	return Ar;
}

FORCEINLINE FArchive& operator<<(Chaos::FChaosArchive& Ar, Dataflow::FLink& Value)
{
	Ar << Value.Input << Value.Output;
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





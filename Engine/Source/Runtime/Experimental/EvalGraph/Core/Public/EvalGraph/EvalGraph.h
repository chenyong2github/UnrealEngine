// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosArchive.h"
#include "EvalGraph/EvalGraphNode.h"
#include "Serialization/Archive.h"

namespace Eg
{
	class FConnectionBase;

	typedef TTuple<FGuid, FGuid> FConnection; // <Input, Output>


	//
	//
	//
	class EVALGRAPHCORE_API FGraph
	{

		FGuid  Guid;
		TArray< TSharedPtr<FNode> > Nodes;
		TArray< FConnection > Connections;
		//TMap<FGuid, TSharedPtr<FConnectionBase> > ConnectionMap;

	public:
		FGraph(FGuid InGuid = FGuid::NewGuid());
		virtual ~FGraph() {}

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

		void ClearConnections(FConnectionBase*);
		void Connect(FConnectionBase* Input, FConnectionBase* Output);
		void Disconnect(FConnectionBase* Input, FConnectionBase* Output);

		virtual void Serialize(FArchive& Ar);

	};
}

FORCEINLINE FArchive& operator<<(FArchive& Ar, Eg::FGraph& Value)
{
	Value.Serialize(Ar);
	return Ar;
}

FORCEINLINE FArchive& operator<<(Chaos::FChaosArchive& Ar, Eg::FGraph& Value)
{
	Value.Serialize(Ar);
	return Ar;
}



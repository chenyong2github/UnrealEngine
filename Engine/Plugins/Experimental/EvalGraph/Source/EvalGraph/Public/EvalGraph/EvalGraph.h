// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


namespace Eg
{
	class FNode;
	class FConnectionTypeBase;

	typedef TTuple<FGuid, FGuid> FConnection;


	//
	//
	//
	class EVALGRAPH_API FGraph
	{

		FGuid  Guid;
		TArray< TSharedPtr<FNode> > Nodes;
		TArray< FConnection > Connections;
		TMap<FGuid, TSharedPtr<FConnectionTypeBase> > ConnectionMap;

	public:
		FGraph(FGuid InGuid = FGuid::NewGuid());

		int NumNodes() { return Nodes.Num(); }

		template<class T> TSharedPtr<T> AddNode(T* InNode)
		{
			TSharedPtr<T> NewNode(InNode);
			Nodes.AddUnique(NewNode);
			return NewNode;
		}

		void RemoveNode(TSharedPtr<FNode> Node);

		void Connect(FConnectionTypeBase* Input, FConnectionTypeBase* Output);
		void Disconnect(FConnectionTypeBase* Input, FConnectionTypeBase* Output);
	};

}


// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowGraph.h"

struct FDataflowNode;

namespace Dataflow
{
	class FConnection;

	struct DATAFLOWCORE_API FNewNodeParameters {
		FGuid Guid;
		FName Type;
		FName Name; 
	};

	//
	//
	//
	class DATAFLOWCORE_API FNodeFactory
	{
		typedef TFunction<FDataflowNode* (const FNewNodeParameters&)> FNewNodeFunction;

		TMap<FName, FNewNodeFunction > ClassMap;

		static FNodeFactory* Instance;
		FNodeFactory() {}

	public:
		~FNodeFactory() { delete Instance; }

		static FNodeFactory* GetInstance()
		{
			if (!Instance)
			{
				Instance = new FNodeFactory();
			}
			return Instance;
		}

		void RegisterNode(const FName& Type, FNewNodeFunction NewFunction)
		{
			if (ensure(!ClassMap.Contains(Type)))
			{
				ClassMap.Add(Type, NewFunction);
			}
		}

		TSharedPtr<FDataflowNode> NewNodeFromRegisteredType(FGraph& Graph, const FNewNodeParameters& Param);

		template<class T> TSharedPtr<T> NewNode(FGraph& Graph, const FNewNodeParameters& Param)
		{
			return Graph.AddNode(new T(Param.Name, Param.Guid));
		}

		TArray<FName> RegisteredNodes() const
		{
			TArray<FName> ReturnNames;
			for (auto Elem : ClassMap) ReturnNames.Add(Elem.Key);
			return ReturnNames;
		}
	};

}


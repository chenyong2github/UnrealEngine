// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EvalGraph/EvalGraph.h"

namespace Eg
{
	class FNode;
	class FConnectionTypeBase;

	struct EVALGRAPHCORE_API FNewNodeParameters {
		FGuid Guid;
		FName Type;
		FName Name; 
	};

	//
	//
	//
	class EVALGRAPHCORE_API FNodeFactory
	{
		typedef TFunction<FNode* (const FNewNodeParameters&)> FNewNodeFunction;

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

		TSharedPtr<FNode> NewNodeFromRegisteredType(FGraph& Graph, const FNewNodeParameters& Param);

		void here() {}
		template<class T> TSharedPtr<T> NewNode(FGraph& Graph, const FNewNodeParameters& Param)
		{
			return Graph.AddNode(new T(Param.Name, Param.Guid));
		}
	};

}


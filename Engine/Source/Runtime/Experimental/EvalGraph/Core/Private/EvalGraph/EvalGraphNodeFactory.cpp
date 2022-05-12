// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphNodeFactory.h"

#include "EvalGraph/EvalGraphNode.h"

namespace Eg
{
	FNodeFactory* FNodeFactory::Instance = nullptr;

	TSharedPtr<FNode> FNodeFactory::NewNodeFromRegisteredType(FGraph& Graph, const FNewNodeParameters& Param)
	{ 
		if (ClassMap.Contains(Param.Type))
		{
			return Graph.AddNode(ClassMap[Param.Type](Param));
		}
		return TSharedPtr<FNode>(nullptr);
	}
}


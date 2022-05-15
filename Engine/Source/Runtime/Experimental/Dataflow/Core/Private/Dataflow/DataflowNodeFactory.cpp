// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowNodeFactory.h"

#include "Dataflow/DataflowNode.h"

namespace Dataflow
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


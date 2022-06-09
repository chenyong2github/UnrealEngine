// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowNodeFactory.h"

#include "Dataflow/DataflowNode.h"

namespace Dataflow
{
	FNodeFactory* FNodeFactory::Instance = nullptr;

	TSharedPtr<FDataflowNode> FNodeFactory::NewNodeFromRegisteredType(FGraph& Graph, const FNewNodeParameters& Param)
	{ 
		if (ClassMap.Contains(Param.Type))
		{
			return Graph.AddNode(ClassMap[Param.Type](Param));
		}
		return TSharedPtr<FDataflowNode>(nullptr);
	}
}


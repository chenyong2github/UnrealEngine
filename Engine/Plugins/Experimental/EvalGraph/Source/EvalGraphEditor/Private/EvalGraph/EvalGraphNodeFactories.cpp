// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphNodeFactories.h"

#include "Logging/LogMacros.h"



TSharedPtr<class SGraphNode> FEvalGraphNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	//
	// @example(EvalGraphNode) Register SNode for display properties in editor. 
	//
	/**
	if (UEvalGraphGraphNodeExample* Node = Cast<UEvalGraphGraphNodeExample>(InNode))
	{
		return SNew(SEvalGraphGraphNodeExample, Node);
	}

	*/
	return NULL;
}

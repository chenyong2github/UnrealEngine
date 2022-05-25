// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowProperty.h"
#include "Dataflow/DataflowNode.h"

namespace Dataflow
{
	void FProperty::BindProperty(FNode* InNode, FProperty* That)
	{
		InNode->AddProperty(That);
	}
}


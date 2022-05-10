// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphNode.h"

#include "EvalGraph/EvalGraphNode.h"

namespace Eg
{
	void FNode::InvalidateOutputs()
	{
		for(FConnectionTypeBase * Output :  Outputs)
		{
			Output->Invalidate();
		}
	}

}


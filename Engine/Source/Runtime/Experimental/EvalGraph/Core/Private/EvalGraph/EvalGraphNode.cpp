// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphNode.h"

#include "EvalGraph/EvalGraphNode.h"

namespace Eg
{
	TArray<FPin> FNode::GetPins() const
	{
		TArray<FPin> RetVal;
		for (FConnectionBase* Con : Inputs)
			RetVal.Add({ FPin::EDirection::INPUT,Con->GetType(), Con->GetName() });
		for (FConnectionBase* Con : Outputs)
			RetVal.Add({ FPin::EDirection::OUTPUT,Con->GetType(), Con->GetName() });
		return RetVal;
	}

	void FNode::InvalidateOutputs()
	{
		for(FConnectionBase * Output :  Outputs)
		{
			Output->Invalidate();
		}
	}

}


// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphNode.h"

#include "EvalGraph/EvalGraphNode.h"

namespace Eg
{
	void FNode::AddBaseInput(FConnectionBase* InPtr)
	{ 
		for (FConnectionBase* In : Inputs)
		{
			ensureMsgf(!In->GetName().IsEqual(InPtr->GetName()), TEXT("Add Input Failed: Existing Node input already defined with name (%s)"), *InPtr->GetName().ToString());
		}
		Inputs.Add(InPtr); 
	}

	void FNode::AddBaseOutput(FConnectionBase* InPtr)
	{ 
		for (FConnectionBase* Out : Outputs)
		{
			ensureMsgf(!Out->GetName().IsEqual(InPtr->GetName()), TEXT("Add Output Failed: Existing Node output already defined with name (%s)"), *InPtr->GetName().ToString());
		}
		Outputs.Add(InPtr);
	}



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

	FConnectionBase* FNode::FindInput(FName InName) const
	{
		for (FConnectionBase* Input : Inputs)
		{
			if (Input->GetName().IsEqual(InName))
			{
				return Input;
			}
		}
		return nullptr;
	}

	FConnectionBase* FNode::FindOutput(FName InName) const
	{
		for (FConnectionBase* Output : Outputs)
		{
			if (Output->GetName().IsEqual(InName))
			{
				return Output;
			}
		}
		return nullptr;
	}

}


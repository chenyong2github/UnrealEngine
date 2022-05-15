// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowNode.h"

#include "Dataflow/DataflowNode.h"

namespace Dataflow
{
	void FNode::AddInput(FConnection* InPtr)
	{ 
		for (FConnection* In : Inputs)
		{
			ensureMsgf(!In->GetName().IsEqual(InPtr->GetName()), TEXT("Add Input Failed: Existing Node input already defined with name (%s)"), *InPtr->GetName().ToString());
		}
		Inputs.Add(InPtr); 
	}

	void FNode::AddOutput(FConnection* InPtr)
	{ 
		for (FConnection* Out : Outputs)
		{
			ensureMsgf(!Out->GetName().IsEqual(InPtr->GetName()), TEXT("Add Output Failed: Existing Node output already defined with name (%s)"), *InPtr->GetName().ToString());
		}
		Outputs.Add(InPtr);
	}



	TArray<FPin> FNode::GetPins() const
	{
		TArray<FPin> RetVal;
		for (FConnection* Con : Inputs)
			RetVal.Add({ FPin::EDirection::INPUT,Con->GetType(), Con->GetName() });
		for (FConnection* Con : Outputs)
			RetVal.Add({ FPin::EDirection::OUTPUT,Con->GetType(), Con->GetName() });
		return RetVal;
	}

	void FNode::InvalidateOutputs()
	{
		for(FConnection * Output :  Outputs)
		{
			Output->Invalidate();
		}
	}

	FConnection* FNode::FindInput(FName InName) const
	{
		for (FConnection* Input : Inputs)
		{
			if (Input->GetName().IsEqual(InName))
			{
				return Input;
			}
		}
		return nullptr;
	}

	FConnection* FNode::FindOutput(FName InName) const
	{
		for (FConnection* Output : Outputs)
		{
			if (Output->GetName().IsEqual(InName))
			{
				return Output;
			}
		}
		return nullptr;
	}

}


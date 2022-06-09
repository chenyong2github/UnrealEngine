// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowNode.h"

#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowArchive.h"

void FDataflowNode::AddInput(Dataflow::FConnection* InPtr)
{
	for (Dataflow::FConnection* In : Inputs)
	{
		ensureMsgf(!In->GetName().IsEqual(InPtr->GetName()), TEXT("Add Input Failed: Existing Node input already defined with name (%s)"), *InPtr->GetName().ToString());
	}
	Inputs.Add(InPtr);
}

void FDataflowNode::AddOutput(Dataflow::FConnection* InPtr)
{
	for (Dataflow::FConnection* Out : Outputs)
	{
		ensureMsgf(!Out->GetName().IsEqual(InPtr->GetName()), TEXT("Add Output Failed: Existing Node output already defined with name (%s)"), *InPtr->GetName().ToString());
	}
	Outputs.Add(InPtr);
}



TArray<Dataflow::FPin> FDataflowNode::GetPins() const
{
	TArray<Dataflow::FPin> RetVal;
	for (Dataflow::FConnection* Con : Inputs)
		RetVal.Add({ Dataflow::FPin::EDirection::INPUT,Con->GetType(), Con->GetName() });
	for (Dataflow::FConnection* Con : Outputs)
		RetVal.Add({ Dataflow::FPin::EDirection::OUTPUT,Con->GetType(), Con->GetName() });
	return RetVal;
}

void FDataflowNode::InvalidateOutputs()
{
	for (Dataflow::FConnection* Output : Outputs)
	{
		Output->Invalidate();
	}
}

Dataflow::FConnection* FDataflowNode::FindInput(FName InName) const
{
	for (Dataflow::FConnection* Input : Inputs)
	{
		if (Input->GetName().IsEqual(InName))
		{
			return Input;
		}
	}
	return nullptr;
}

Dataflow::FConnection* FDataflowNode::FindOutput(FName InName) const
{
	for (Dataflow::FConnection* Output : Outputs)
	{
		if (Output->GetName().IsEqual(InName))
		{
			return Output;
		}
	}
	return nullptr;
}




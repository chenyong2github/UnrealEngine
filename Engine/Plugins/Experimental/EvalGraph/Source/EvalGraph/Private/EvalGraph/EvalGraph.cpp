// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraph.h"

#include "EvalGraph/EvalGraphInputOutput.h"
#include "EvalGraph/EvalGraphConnectionTypes.h"
#include "EvalGraph/EvalGraphNodeParameters.h"
#include "EvalGraph/EvalGraphNode.h"

namespace Eg
{

	FGraph::FGraph(FGuid InGuid)
		: Guid(InGuid)
	{}

	void FGraph::RemoveNode(TSharedPtr<FNode> Node)
	{
		for (FConnectionTypeBase* Output : Node->GetOutputs())
		{
			if (Output)
			{
				for (FConnectionTypeBase* Input : Output->GetBaseInputs())
				{
					if (Input)
					{
						Disconnect(Output, Input);
					}
				}
			}
		}
		for (FConnectionTypeBase* Input : Node->GetInputs())
		{
			if (Input)
			{
				TArray<FConnectionTypeBase*> Outputs = Input->GetBaseOutputs();
				for (FConnectionTypeBase* Out : Outputs)
				{
					if (Out)
					{
						Disconnect(Out, Input);
					}
				}
			}
		}
		Nodes.Remove(Node);
	}

	void FGraph::Connect(FConnectionTypeBase* Input, FConnectionTypeBase* Output)
	{
		if (ensure(Input && Output))
		{
			Input->AddConnection(Output);
			Output->AddConnection(Input);
			Connections.Add(FConnection(Input->GetGuid(), Output->GetGuid()));
		}
	}

	void FGraph::Disconnect(FConnectionTypeBase* Input, FConnectionTypeBase* Output)
	{
		Input->RemoveConnection(Output);
		Output->RemoveConnection(Input);
		Connections.RemoveSwap(FConnection(Input->GetGuid(), Output->GetGuid()));
	}


}


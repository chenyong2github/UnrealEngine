// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryFlowGraph.h"

using namespace UE::GeometryFlow;


TSafeSharedPtr<FNode> FGraph::FindNode(FHandle NodeHandle) const
{
	const FNodeInfo* Found = AllNodes.Find(NodeHandle);
	if (!ensure(Found != nullptr))
	{
		return nullptr;
	}
	return Found->Node;
}

EGeometryFlowResult FGraph::GetInputTypeForNode(FHandle NodeHandle, FString InputName, int32& Type) const
{
	const FNodeInfo* Found = AllNodes.Find(NodeHandle);
	if (!ensure(Found != nullptr))
	{
		return EGeometryFlowResult::NodeDoesNotExist;
	}
	EGeometryFlowResult Result = Found->Node->GetInputType(InputName, Type);
	return Result;
}

EGeometryFlowResult FGraph::GetOutputTypeForNode(FHandle NodeHandle, FString OutputName, int32& Type) const
{
	const FNodeInfo* Found = AllNodes.Find(NodeHandle);
	if (!ensure(Found != nullptr))
	{
		return EGeometryFlowResult::NodeDoesNotExist;
	}
	EGeometryFlowResult Result = Found->Node->GetOutputType(OutputName, Type);
	return Result;
}


ENodeCachingStrategy FGraph::GetCachingStrategyForNode(FHandle NodeHandle) const
{
	const FNodeInfo* Found = AllNodes.Find(NodeHandle);
	if (!ensure(Found != nullptr))
	{
		return ENodeCachingStrategy::AlwaysCache;
	}
	return (Found->CachingStrategy == ENodeCachingStrategy::Default) ?
		DefaultCachingStrategy : Found->CachingStrategy;
}


EGeometryFlowResult FGraph::AddConnection(FHandle FromNode, FString FromOutput, FHandle ToNode, FString ToInput)
{
	int32 FromType = -1, ToType = -2;
	EGeometryFlowResult FromTypeResult = GetOutputTypeForNode(FromNode, FromOutput, FromType);
	if (!ensure(FromTypeResult == EGeometryFlowResult::Ok))
	{
		return FromTypeResult;
	}
	EGeometryFlowResult ToTypeResult = GetInputTypeForNode(ToNode, ToInput, ToType);
	if (!ensure(ToTypeResult == EGeometryFlowResult::Ok))
	{
		return ToTypeResult;
	}

	if (!ensure(FromType == ToType))
	{
		return EGeometryFlowResult::UnmatchedTypes;
	}

	// TODO: check for unique inputs

	Connections.Add({ FromNode, FromOutput, ToNode, ToInput });

	return EGeometryFlowResult::Ok;
}



EGeometryFlowResult FGraph::InferConnection(FHandle FromNodeHandle, FHandle ToNodeHandle)
{
	TSafeSharedPtr<FNode> FromNode = FindNode(FromNodeHandle);
	TSafeSharedPtr<FNode> ToNode = FindNode(ToNodeHandle);
	if (FromNode == nullptr || ToNode == nullptr)
	{
		ensure(false);
		return EGeometryFlowResult::NodeDoesNotExist;
	}

	FString FromOutputName;
	FString ToInputName;
	int32 TotalMatchesFound = 0;
	FromNode->EnumerateOutputs([&](const FString& OutputName, const TUniquePtr<INodeOutput>& Output)
	{
		int32 OutputType = Output->GetDataType();
		ToNode->EnumerateInputs([&](const FString& InputName, const TUniquePtr<INodeInput>& Input)
		{
			int32 InputType = Input->GetDataType();
			if (OutputType == InputType)
			{
				TotalMatchesFound++;
				FromOutputName = OutputName;
				ToInputName = InputName;
			}
		});
	});
	ensure(TotalMatchesFound == 1);
	switch (TotalMatchesFound)
	{
	case 1:
		return AddConnection(FromNodeHandle, FromOutputName, ToNodeHandle, ToInputName);
	case 0:
		return EGeometryFlowResult::NoMatchesFound;
	default:
		return EGeometryFlowResult::MultipleMatchingAmbiguityFound;
	}
}





EGeometryFlowResult FGraph::FindConnectionForInput(FHandle ToNode, FString ToInput, FConnection& ConnectionOut) const
{
	for (const FConnection& Connection : Connections)
	{
		if (Connection.ToNode == ToNode && Connection.ToInput == ToInput)
		{
			ConnectionOut = Connection;
			return EGeometryFlowResult::Ok;
		}
	}
	ensure(false);
	return EGeometryFlowResult::ConnectionDoesNotExist;
}


int32 FGraph::CountOutputConnections(FHandle FromNode, const FString& OutputName) const
{
	int32 Count = 0;
	for (const FConnection& Connection : Connections)
	{
		if (Connection.FromNode == FromNode && Connection.FromOutput == OutputName)
		{
			Count++;
		}
	}
	return Count;
}



TSafeSharedPtr<IData> FGraph::ComputeOutputData(
	FHandle NodeHandle, 
	FString OutputName, 
	TUniquePtr<FEvaluationInfo>& EvaluationInfo,
	bool bStealOutputData)
{
	TSafeSharedPtr<FNode> Node = FindNode(NodeHandle);
	check(Node);

	// figure out which upstream Connections/Inputs we need to compute this Output
	TArray<FString> Outputs;
	Outputs.Add(OutputName);
	TArray<FEvalRequirement> InputRequirements;
	Node->CollectRequirements({ OutputName }, InputRequirements);

	// this is the map of (InputName, Data) we will build up by pulling from the Connections
	FNamedDataMap DataIn;

	// Collect data from those Inputs.
	// This will recursively call ComputeOutputData() on those (Node/Output) pairs
	for (int32 k = 0; k < InputRequirements.Num(); ++k)
	{
		FDataFlags DataFlags;
		const FString& InputName = InputRequirements[k].InputName;

		// find the connection for this input
		FConnection Connection;
		EGeometryFlowResult FoundResult = FindConnectionForInput(NodeHandle, InputName, Connection);
		check(FoundResult == EGeometryFlowResult::Ok);

		ENodeCachingStrategy FromCachingStrategy = GetCachingStrategyForNode(Connection.FromNode);

		// If there is only one Connection from this upstream Output (ie to our Input), and the Node/Input 
		// can steal and transform that data, then we will do it
		int32 OutputUsageCount = CountOutputConnections(Connection.FromNode, Connection.FromOutput);
		bool bStealDataForInput = false;
		if (OutputUsageCount == 1 
			&& InputRequirements[k].InputFlags.bCanTransformInput
			&& FromCachingStrategy != ENodeCachingStrategy::AlwaysCache)
		{
			bStealDataForInput = true;
			DataFlags.bIsMutableData = true;
		}

		// recursively fetch Data coming in to this Input via Connection
		TSafeSharedPtr<IData> OutputData = 
			ComputeOutputData(Connection.FromNode, Connection.FromOutput, EvaluationInfo, bStealDataForInput);
	
		// add to named data map
		DataIn.Add(InputName, OutputData, DataFlags);
	}

	// evalute node
	FNamedDataMap DataOut;
	DataOut.Add(OutputName);
	Node->Evaluate(DataIn, DataOut, EvaluationInfo);
	EvaluationInfo->CountEvaluation(Node.Get());

	// collect (and optionally take/steal) desired Output data
	TSafeSharedPtr<IData> Result = (bStealOutputData) ? 
		Node->StealOutput(OutputName) : DataOut.FindData(OutputName);

	check(Result);
	return Result;
}





void FGraph::ConfigureCachingStrategy(ENodeCachingStrategy NewStrategy)
{
	if (NewStrategy != DefaultCachingStrategy && ensure(NewStrategy != ENodeCachingStrategy::Default) )
	{
		DefaultCachingStrategy = NewStrategy;

		// todo: clear caches if necessary?
	}
}


EGeometryFlowResult FGraph::SetNodeCachingStrategy(FHandle NodeHandle, ENodeCachingStrategy Strategy)
{
	FNodeInfo* Found = AllNodes.Find(NodeHandle);
	if (!ensure(Found != nullptr))
	{
		return EGeometryFlowResult::NodeDoesNotExist;
	}
	Found->CachingStrategy = Strategy;
	return EGeometryFlowResult::Ok;
}
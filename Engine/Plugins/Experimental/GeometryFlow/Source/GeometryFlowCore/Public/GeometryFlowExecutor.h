// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryFlowGraph.h"
#include "Async/TaskGraphInterfaces.h"
#include "Util/ProgressCancel.h"

namespace UE
{
namespace GeometryFlow
{

///
/// Parallel execution using TaskGraph
///
class GEOMETRYFLOWCORE_API FGeometryFlowExecutor
{

public:

	FGeometryFlowExecutor(FGraph& InGraph);

	struct NodeOutputSpec
	{
		FGraph::FHandle NodeHandle;
		FString OutputName;
		bool bStealOutput;
	};

	// There are two ways to run the graph and get outputs:
	// 1. Call ComputeOutputs, which is a synchronous call that will execute the graph and grab the outputs, or
	// 2. First call AsyncRunGraph to launch the execution, then call GetOutput for each output.

	// 1.
	// Do graph execution, wait for it to finish, and get the requested output data.
	// NOTE: If NodeOutputSpec::bStealOutput is false for a given output, you must *not* call IData::GiveTo on the 
	// corresponding output data!
	EGeometryFlowResult ComputeOutputs(const TArray<NodeOutputSpec>& DesiredOutputs,
									   TArray<TSafeSharedPtr<IData>>& OutputDatas,
									   FProgressCancel* Progress = nullptr);

	// 2a.
	// Launch graph execution on background threads and return
	void AsyncRunGraph(FProgressCancel* Progress);

	// 2b.
	// Wait for a given node to finish executing, then grab the requested data
	template<typename T>
	EGeometryFlowResult GetOutput(const FGraph::FHandle& NodeHandle,
								  const FString& OutputName,
								  T& Storage,
								  int32 StorageTypeIdentifier,
								  bool bStealOutputData)
	{
		FGraphEventRef* GraphEvent = GeometryFlowNodeToGraphTask.Find(NodeHandle);

		if (GraphEvent == nullptr)
		{
			return EGeometryFlowResult::NoMatchesFound;
		}

		// Wait for the node to finish executing
		(*GraphEvent)->Wait();

		if (EvalInfo && EvalInfo->Progress && EvalInfo->Progress->Cancelled())
		{
			return EGeometryFlowResult::OperationCancelled;
		}	
		
		TSafeSharedPtr<FNode> GeoFlowNode = GeometryFlowGraph.FindNode(NodeHandle);
		if (!GeoFlowNode)
		{
			return EGeometryFlowResult::NodeDoesNotExist;
		}

		TSafeSharedPtr<IData> OutputData = (bStealOutputData) ?
			GeoFlowNode->StealOutput(OutputName) : GeoFlowNode->GetOutput(OutputName);

		if (!OutputData)
		{
			return EGeometryFlowResult::OutputDoesNotExist;
		}

		if (OutputData->GetPayloadType() != StorageTypeIdentifier)
		{
			return EGeometryFlowResult::UnmatchedTypes;
		}
		if (bStealOutputData)
		{
			OutputData->GiveTo(Storage, StorageTypeIdentifier);
		}
		else
		{
			OutputData->GetDataCopy(Storage, StorageTypeIdentifier);
		}

		return EGeometryFlowResult::Ok;
	}

	TUniquePtr<FEvaluationInfo> EvalInfo;

protected:

	FGraph GeometryFlowGraph;

	TArray<FGraph::FHandle> TopologicallySortedNodes;
	void TopologicalSort();

	TMap<FGraph::FHandle, FGraphEventRef> GeometryFlowNodeToGraphTask;

};

}
}

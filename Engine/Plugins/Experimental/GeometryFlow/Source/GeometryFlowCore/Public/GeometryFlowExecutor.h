// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryFlowGraph.h"
#include "Async/TaskGraphInterfaces.h"

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

	using NodeOutputSpec = TPair<FGraph::FHandle, FString>;		// (Node handle, output name)
	void ComputeOutputs(const TArray<NodeOutputSpec>& DesiredOutputs, TArray<TSafeSharedPtr<IData>>& OutputDatas);

protected:

	FGraph GeometryFlowGraph;

	TArray<FGraph::FHandle> TopologicallySortedNodes;
	void TopologicalSort();

	TMap<FGraph::FHandle, FGraphEventRef> GeometryFlowNodeToGraphTask;
	void CreateAndDispatchTaskGraph();

	// Debug helpers
	TArray<int32> DebugNodeExecutionLog;		        // when a node executes it adds its ID to this array
	TMap<FNode*, double> DebugNodeExecutionTime;		// log execution time
	FCriticalSection DebugNodeExecutionLogLock;

};

}
}

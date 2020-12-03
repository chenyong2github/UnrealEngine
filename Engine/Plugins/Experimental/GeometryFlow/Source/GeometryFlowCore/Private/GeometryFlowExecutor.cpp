// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryFlowExecutor.h"
#include "GeometryFlowGraphUtil.h"

using namespace UE::GeometryFlow;


namespace
{
	template<typename T>
	bool PopElementFromSet(TSet<T>& Set, T& Element)
	{
		typename TSet<T>::TIterator Iter(Set);
		if (!Iter)
		{
			return false;
		}
		else
		{
			Element = *Iter;
			Iter.RemoveCurrent();
			return true;
		}
	}
}

FGeometryFlowExecutor::FGeometryFlowExecutor(FGraph& InGraph)
	: GeometryFlowGraph(InGraph)
{
	TopologicalSort();
}


void FGeometryFlowExecutor::ComputeOutputs(const TArray<NodeOutputSpec>& DesiredOutputs,
										   TArray<TSafeSharedPtr<IData>>& OutputDatas)
{
	// TODO: Improve this!

	CreateAndDispatchTaskGraph();

	for (const NodeOutputSpec& OutputSpec : DesiredOutputs)
	{
		for (const TPair<FGraph::FHandle, FGraphEventRef>& NodeRef : GeometryFlowNodeToGraphTask)
		{
			FGraph::FHandle NodeHandle{ NodeRef.Key };
			if ( OutputSpec.Key == NodeHandle)
			{ 
				// TODO: Don't wait for each one, see if we can set a callback for when a task is done
				NodeRef.Value->Wait();

				// TODO: Check desired data type matches node output type

				TSafeSharedPtr<FNode> GeoFlowNode = GeometryFlowGraph.FindNode(NodeHandle);
				TSafeSharedPtr<IData> OutputData = GeoFlowNode->GetOutput(OutputSpec.Value);
				check(OutputData != nullptr);

				OutputDatas.Add(OutputData);
			}
		}
	}

}




void FGeometryFlowExecutor::TopologicalSort()
{
	// Kahn's algorithm from Wikipedia

	TArray<FGraph::FConnection> Edges = GeometryFlowGraph.Connections;

	// L := Empty list that will contain the sorted elements
	TopologicallySortedNodes.Empty();

	// S := Set of all nodes with no incoming edge
	TSet<FGraph::FHandle> CurrentNodes = GeometryFlowGraph.GetSourceNodes();

	while (CurrentNodes.Num() != 0)
	{
		// remove a node n from S
		FGraph::FHandle N;
		bool bPopOK = PopElementFromSet(CurrentNodes, N);
		check(bPopOK);

		// add n to L
		TopologicallySortedNodes.Add(N);

		// for each node m with an edge e from n to m do
		int FoundEdgeIndex = FindAnyConnectionFromNode(N, Edges);
		while (FoundEdgeIndex >= 0)
		{
			FGraph::FHandle M = Edges[FoundEdgeIndex].ToNode;

			// remove edge e from the graph
			Edges.RemoveAt(FoundEdgeIndex);

			// if m has no other incoming edges then
			if (FindAnyConnectionToNode(M, Edges) < 0)
			{
				// insert m into S
				CurrentNodes.Add(M);
			}

			FoundEdgeIndex = FindAnyConnectionFromNode(N, Edges);
		}
	}

	check(Edges.Num() == 0);		// If any edges remain there's a cycle in the graph
}


void FGeometryFlowExecutor::CreateAndDispatchTaskGraph()
{
	DebugNodeExecutionLog.Reset();
	DebugNodeExecutionTime.Reset();

	// Track nodes as they are added to the TaskGraph
	GeometryFlowNodeToGraphTask.Reset();

	// We'll add nodes to the task graph in topologically sorted order. This means any prereqs for a node should 
	// already be in this container when the node is processed.

	for (FGraph::FHandle NodeHandle : TopologicallySortedNodes)
	{
		// Find prereqs for the current node
		TArray<int32> IncomingConnections = FindAllConnectionsToNode(NodeHandle, GeometryFlowGraph.Connections);
		FGraphEventArray Prereqs;
		for (int32 ConnectionIndex : IncomingConnections)
		{
			check(GeometryFlowGraph.Connections[ConnectionIndex].ToNode == NodeHandle);
			FGraph::FHandle M = GeometryFlowGraph.Connections[ConnectionIndex].FromNode;
			check(GeometryFlowNodeToGraphTask.Find(M) != nullptr);		// should have been already added to TaskGraph
			Prereqs.Add(GeometryFlowNodeToGraphTask[M]);
		}

		// Now construct the node and add it to the list
		
		auto TaskLambda = [this, NodeHandle]()
		{
			FGraph::FNodeInfo* NodeInfo = GeometryFlowGraph.AllNodes.Find(NodeHandle);
			check(NodeInfo != nullptr);

			TSafeSharedPtr<FNode> Node = NodeInfo->Node;
			check(Node != nullptr);

			// this is the map of (InputName, Data) we will build up by pulling from the Connections
			FNamedDataMap DatasIn;

			// Collect data from those Inputs.
			Node->EnumerateInputs([this, NodeHandle, &DatasIn, &Node] (const FString& InputName, const TUniquePtr<INodeInput>& Input)
			{
				// find the connection for this input
				FGraph::FConnection Connection;
				EGeometryFlowResult FoundResult = GeometryFlowGraph.FindConnectionForInput(NodeHandle, InputName, Connection);
				check(FoundResult == EGeometryFlowResult::Ok);

				// If there is only one Connection from this upstream Output (ie to our Input), and the Node/Input 
				// can steal and transform that data, then we will do it
				int32 OutputUsageCount = GeometryFlowGraph.CountOutputConnections(Connection.FromNode, Connection.FromOutput);
				bool bStealDataForInput = false;
				FDataFlags DataFlags;

				ENodeCachingStrategy FromCachingStrategy = GeometryFlowGraph.GetCachingStrategyForNode(Connection.FromNode);
				if (OutputUsageCount == 1
					&& Input->CanTransformInput()
					&& FromCachingStrategy != ENodeCachingStrategy::AlwaysCache)
				{
					bStealDataForInput = true;
					DataFlags.bIsMutableData = true;
				}

				// TODO: Error if upstream node hasn't executed yet.

				FGraph::FHandle UpstreamNodeHandle = Connection.FromNode;
				TSafeSharedPtr<FNode> UpstreamNode = GeometryFlowGraph.FindNode(UpstreamNodeHandle);
				FString UpstreamOutputName = Connection.FromOutput;
				TSafeSharedPtr<IData> OutputData = UpstreamNode->GetOutput(UpstreamOutputName);

				TSafeSharedPtr<IData> Result = (bStealDataForInput) ?
					UpstreamNode->StealOutput(UpstreamOutputName) : UpstreamNode->GetOutput(UpstreamOutputName);

				// add to named data map
				DatasIn.Add(InputName, OutputData, DataFlags);
			});

			// evalute node
			FNamedDataMap DatasOut;
			for (const FNode::FNodeOutputInfo& NodeOutput : Node->NodeOutputs)
			{
				DatasOut.Add(NodeOutput.Name);
			}

			double T0 = FPlatformTime::Seconds();
			
			TUniquePtr<FEvaluationInfo> EvalInfo = MakeUnique<FEvaluationInfo>();
			Node->Evaluate(DatasIn, DatasOut, EvalInfo);

			double T1 = FPlatformTime::Seconds();

			// Debug logging
			DebugNodeExecutionLogLock.Lock();
			DebugNodeExecutionLog.Add(NodeHandle.Identifier);
			DebugNodeExecutionTime.Add(Node.Get(), T1 - T0);
			DebugNodeExecutionLogLock.Unlock();
		};
		
		// TODO: Create but don't dispatch? Possible?
		FGraphEventRef TaskGraphN = FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(TaskLambda), TStatId{}, &Prereqs);

		TPair<FGraph::FHandle, FGraphEventRef> NodeInfo{ NodeHandle, TaskGraphN };
		GeometryFlowNodeToGraphTask.Add(NodeInfo);

	}
}


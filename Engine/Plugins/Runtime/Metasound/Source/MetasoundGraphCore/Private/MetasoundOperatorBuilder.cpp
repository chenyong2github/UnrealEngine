// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundOperatorBuilder.h"

#include "MetasoundBuilderInterface.h"
#include "MetasoundBuildError.h"
#include "MetasoundGraphAlgo.h"
#include "MetasoundGraphOperator.h"
#include "MetasoundOperatorInterface.h"


namespace Metasound
{
	namespace OperatorBuilderPrivate
	{
		using FBuildErrorPointer = TUniquePtr<IOperatorBuildError>;

		template<typename ErrorType, typename... ArgTypes>
		void AddBuildError(TArray<FBuildErrorPointer>& OutErrors, ArgTypes&&... Args)
		{
			OutErrors.Add(MakeUnique<ErrorType>(Forward<ArgTypes>(Args)...));
		}

		void GetEdgesBetweenNodes(const TArray<INode*>& InNodes, const TArray<FDataEdge>& InEdges, TArray<FDataEdge>& OutEdges)
		{
			OutEdges = InEdges.FilterByPredicate(
				[&](const FDataEdge& InEdge) 
				{
					bool bEdgeInNodes = InNodes.Contains(InEdge.To.Node);
					bEdgeInNodes &= InNodes.Contains(InEdge.From.Node);

					return bEdgeInNodes;
				}
			);
		}
	}

	FOperatorBuilder::FOperatorBuilder(const FOperatorSettings& InSettings)
	:	OperatorSettings(InSettings)
	{
	}

	FOperatorBuilder::~FOperatorBuilder()
	{
	}

	TUniquePtr<IOperator> FOperatorBuilder::BuildGraphOperator(const IGraph& InGraph, TArray<FBuildErrorPtr>& OutErrors)
	{
		TArray<INode*> SortedNodes;

		FNodeEdgeMultiMap NodeInputEdges;

		bool bSuccess = true;


		bSuccess = GroupInputEdges(InGraph.GetDataEdges(), NodeInputEdges, OutErrors);
		if (!bSuccess)
		{
			return TUniquePtr<IOperator>(nullptr);
		}

		// TODO: Validate edges.

		bSuccess = TopologicalSort(InGraph, SortedNodes, OutErrors);
		if (!bSuccess)
		{
			return TUniquePtr<IOperator>(nullptr);
		}

		// TODO: Add graph pruning step.

		TArray<TUniquePtr<IOperator>> Operators;
		FNodeDataReferenceMap NodeData;

		bSuccess = CreateOperators(SortedNodes, NodeInputEdges, Operators, NodeData, OutErrors);
		if (!bSuccess)
		{
			return TUniquePtr<IOperator>(nullptr);
		}

		return CreateGraphOperator(InGraph, Operators, NodeData, OutErrors);
	}

	bool FOperatorBuilder::GroupInputEdges(const TArray<FDataEdge>& InEdges, FNodeEdgeMultiMap& OutNodeInputs, TArray<FBuildErrorPtr>& OutErrors) const
	{
		for (const FDataEdge& Edge : InEdges)
		{
			OutNodeInputs.Add(Edge.To.Node, &Edge);
		}

		// TODO: error on duplicate inputs.
		
		return true;
	}

	bool FOperatorBuilder::TopologicalSort(const IGraph& InGraph, TArray<INode*>& OutNodes, TArray<FBuildErrorPtr>& OutErrors) const
	{
		using namespace OperatorBuilderPrivate;
		using FNodePair = TPair<INode*, INode*>;
		using FNodeMultiMap = TMultiMap<INode*, INode*>;
		// TODO: Implement depth first topological sort to improve cache coherency.
		
		TArray<INode*> UniqueNodes;
		FNodeMultiMap Dependencies;

		for (const FDataEdge& Edge : InGraph.GetDataEdges())
		{
			if (nullptr == Edge.To.Node)
			{
				// Edges are required to be connected to a destination node.
				AddBuildError<FDanglingEdgeError>(OutErrors, Edge);

				continue;
			}

			if (nullptr == Edge.From.Node)
			{
				// Edges are required to be connected to a source node.
				AddBuildError<FDanglingEdgeError>(OutErrors, Edge);

				continue;
			}

			Dependencies.AddUnique(Edge.To.Node, Edge.From.Node);
			UniqueNodes.AddUnique(Edge.To.Node);
			UniqueNodes.AddUnique(Edge.From.Node);
		}


		// Sort graph so that nodes with no dependencies always go first.
		while (UniqueNodes.Num() > 0)
		{
			TArray<INode*> IndependentNodes = UniqueNodes.FilterByPredicate([&](INode* InNode) { 
					return (Dependencies.Num(InNode) == 0);
			});

			if (0 == IndependentNodes.Num())
			{
				// likely a cycle in the graph.
				
				bool bExcludeSingleVertex = true;

				// Try to find cycles
				TArray<FStronglyConnectedComponent> Cycles;

				if(FDirectedGraphAlgo::TarjanStronglyConnectedComponents(InGraph, Cycles, bExcludeSingleVertex))
				{
					for (const FStronglyConnectedComponent& Cycle : Cycles)
					{
						AddBuildError<FGraphCycleError>(OutErrors, Cycle.Nodes, Cycle.Edges);
					}
				}
				else
				{
					AddBuildError<FInternalError>(OutErrors, TEXT(__FILE__), __LINE__);
				}

				return false;
			}

			FNodeMultiMap UpdatedDependencies;
			
			// Remove independent nodes from dependency map.
			for (FNodePair& Element : Dependencies)
			{
				if (!IndependentNodes.Contains(Element.Value))
				{
					UpdatedDependencies.AddUnique(Element.Key, Element.Value);
				}
			}
			Dependencies = UpdatedDependencies;

			// Remove independent nodes from node list
			for (INode* Node : IndependentNodes)
			{
				UniqueNodes.RemoveSwap(Node);
			}

			// Add independent nodes to output.
			OutNodes.Append(IndependentNodes);
		}

		return true;
	}

	bool FOperatorBuilder::GatherInputDataReferences(const INode* InNode, const FNodeEdgeMultiMap& InEdgeMap, const FNodeDataReferenceMap& InDataReferenceMap, FDataReferenceCollection& OutCollection, TArray<FBuildErrorPtr>& OutErrors) const
	{
		using namespace OperatorBuilderPrivate;

		// Find all input edges associated with this node. 
		TArray<const FDataEdge*> Edges;
		InEdgeMap.MultiFind(InNode, Edges);

		// Add parameters to collection based on edge description
		for (const FDataEdge* Edge : Edges)
		{
			if (!InDataReferenceMap.Contains(Edge->From.Node))
			{
				// This is likely due to a failed topological sort and is more of an internal error
				// than a user error.
				AddBuildError<FInternalError>(OutErrors, TEXT(__FILE__), __LINE__);

				return false;
			}

			
			const FDataReferenceCollection& FromDataReferenceCollection = InDataReferenceMap[Edge->From.Node].Outputs;

			if (!FromDataReferenceCollection.ContainsDataReadReference(Edge->From.Vertex.VertexName, Edge->From.Vertex.DataReferenceTypeName))
			{
				// This is likely a node programming error where the edges reported by the INode interface
				// did not match the readable parameter refs created by the operators outputs. Or, the edge description is invalid.

				// TODO: consider checking that outputs of node operators match output descriptions of nodes. 
				// TODO: consider checking the edges are supported by nodes existing parameter descriptions.
				AddBuildError<FMissingOutputDataReferenceError>(OutErrors, Edge->From);

				return false;
			}

			if (Edge->From.Vertex.DataReferenceTypeName != Edge->To.Vertex.DataReferenceTypeName)
			{
				// TODO: may want to have this check higher up in the stack. 
				AddBuildError<FInvalidConnectionDataTypeError>(OutErrors, *Edge);

				return false;
			}

			bool bSuccess = OutCollection.AddDataReadReferenceFrom(Edge->To.Vertex.VertexName, FromDataReferenceCollection, Edge->From.Vertex.VertexName, Edge->From.Vertex.DataReferenceTypeName);

			if (!bSuccess)
			{
				AddBuildError<FMissingOutputDataReferenceError>(OutErrors, Edge->From);
				return false;
			}
		}

		return true;
	}

	bool FOperatorBuilder::CreateOperators(const TArray<INode*>& InSortedNodes, FNodeEdgeMultiMap& InNodeInputEdges, TArray<TUniquePtr<IOperator>>& OutOperators, FNodeDataReferenceMap& OutDataReferences, TArray<FBuildErrorPtr>& OutErrors)
	{
		for (INode* Node : InSortedNodes)
		{
			FDataReferenceCollection InputCollection;

			// Gather the input parameters for this IOperator from the output parameters of already created IOperators. 
			if (!GatherInputDataReferences(Node, InNodeInputEdges, OutDataReferences, InputCollection, OutErrors))
			{
				return false;
			}

			IOperatorFactory& Factory = Node->GetDefaultOperatorFactory();

			FOperatorPtr Operator = Factory.CreateOperator(*Node, OperatorSettings, InputCollection, OutErrors);

			if (!Operator.IsValid())
			{
				return false;
			}

			// Save input and output collection for future use 
			OutDataReferences.Emplace(Node, FOperatorDataReferences(Operator->GetInputs(), Operator->GetOutputs()));

			// Add operator to operator array
			OutOperators.Add(MoveTemp(Operator));
		}

		return true;
	}

	bool FOperatorBuilder::GatherGraphDataReferences(const IGraph& InGraph, FNodeDataReferenceMap& InNodeDataReferences, FDataReferenceCollection& OutGraphInputs, FDataReferenceCollection& OutGraphOutputs, TArray<FBuildErrorPtr>& OutErrors) const
	{
		using namespace OperatorBuilderPrivate;
		using FDestinationElement = FInputDataDestinationCollection::ElementType;
		using FSourceElement = FOutputDataSourceCollection::ElementType;

		// Gather inputs
		for (const FDestinationElement& Element : InGraph.GetInputDataDestinations())
		{
			const FInputDataDestination& InputDestination = Element.Value;

			if (!InNodeDataReferences.Contains(InputDestination.Node))
			{
				AddBuildError<FMissingInputDataReferenceError>(OutErrors, InputDestination);
				// This indicates an input node wasn't connected in some way to the output, and was culled earlier.
				continue;
			}

			FDataReferenceCollection& Collection = InNodeDataReferences[InputDestination.Node].Inputs;

			if (!Collection.ContainsDataWriteReference(InputDestination.Vertex.VertexName, InputDestination.Vertex.DataReferenceTypeName))
			{
				AddBuildError<FMissingInputDataReferenceError>(OutErrors, InputDestination);
				return false;
			}

			bool bSuccess = OutGraphInputs.AddDataWriteReferenceFrom(InputDestination.Vertex.VertexName, Collection, InputDestination.Vertex.VertexName, InputDestination.Vertex.DataReferenceTypeName);

			if (!bSuccess)
			{
				AddBuildError<FMissingInputDataReferenceError>(OutErrors, InputDestination);
				return false;
			}
		}

		// Gather outputs
		for (const FSourceElement& Element : InGraph.GetOutputDataSources())
		{
			const FOutputDataSource& OutputSource = Element.Value;

			if (!InNodeDataReferences.Contains(OutputSource.Node))
			{
				AddBuildError<FMissingOutputDataReferenceError>(OutErrors, OutputSource);
				continue;
			}

			FDataReferenceCollection& Collection = InNodeDataReferences[OutputSource.Node].Outputs;

			if (!Collection.ContainsDataReadReference(OutputSource.Vertex.VertexName, OutputSource.Vertex.DataReferenceTypeName))
			{
				AddBuildError<FMissingOutputDataReferenceError>(OutErrors, OutputSource);
				return false;
			}

			bool bSuccess = OutGraphOutputs.AddDataReadReferenceFrom(OutputSource.Vertex.VertexName, Collection, OutputSource.Vertex.VertexName, OutputSource.Vertex.DataReferenceTypeName);

			if (!bSuccess)
			{
				AddBuildError<FMissingOutputDataReferenceError>(OutErrors, OutputSource);
				return false;
			}
		}
	
		return true;
	}

	TUniquePtr<IOperator> FOperatorBuilder::CreateGraphOperator(const IGraph& InGraph, TArray<FOperatorPtr>& InOperators, FNodeDataReferenceMap& InNodeDataReferences, TArray<FBuildErrorPtr>& OutErrors) const
	{
		FDataReferenceCollection GraphInputs;
		FDataReferenceCollection GraphOutputs;

		bool bSuccess = GatherGraphDataReferences(InGraph, InNodeDataReferences, GraphInputs, GraphOutputs, OutErrors);

		if (!bSuccess)
		{
			return TUniquePtr<IOperator>(nullptr);
		}

		TUniquePtr<FGraphOperator> GraphOperator = MakeUnique<FGraphOperator>();

		GraphOperator->SetInputs(GraphInputs);
		GraphOperator->SetOutputs(GraphOutputs);

		for (FOperatorPtr& Ptr : InOperators)
		{
			GraphOperator->AppendOperator(MoveTemp(Ptr));
		}

		InOperators.Reset();

		return MoveTemp(GraphOperator);
	}
}

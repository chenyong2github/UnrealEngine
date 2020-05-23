// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundOperatorBuilder.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundGraphOperator.h"

namespace Metasound
{
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
			// TODO: add error to error stack.
			return TUniquePtr<IOperator>(nullptr);
		}

		// TODO: Validate edges.

		// TODO: need to include inputs/outputs vertices here. 
		bSuccess = TopologicalSort(InGraph.GetDataEdges(), SortedNodes, OutErrors);
		if (!bSuccess)
		{
			// TODO: add error to error stack.
			return TUniquePtr<IOperator>(nullptr);
		}

		// TODO: Add graph pruning step.

		TArray<TUniquePtr<IOperator>> Operators;
		FNodeDataReferenceMap NodeData;

		bSuccess = CreateOperators(SortedNodes, NodeInputEdges, Operators, NodeData, OutErrors);
		if (!bSuccess)
		{
			// TODO: add error to error stack.
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

	bool FOperatorBuilder::TopologicalSort(const TArray<FDataEdge>& InEdges, TArray<INode*>& OutNodes, TArray<FBuildErrorPtr>& OutErrors) const
	{
		using FNodePair = TPair<INode*, INode*>;
		using FNodeMultiMap = TMultiMap<INode*, INode*>;
		// TODO: Implement depth first topological sort to imporove cache coherency.
		
		TArray<INode*> UniqueNodes;
		FNodeMultiMap Dependencies;

		for (const FDataEdge& Edge : InEdges)
		{
			if (nullptr == Edge.To.Node)
			{
				// TODO: add build error for unconnected edge.
				continue;
			}
			if (nullptr == Edge.From.Node)
			{
				// TODO: add build error for unconnected edge.
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
				// TODO: add build error. likely a cycle in the graph.
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
		// Find all input edges associated with this node. 
		TArray<const FDataEdge*> Edges;
		InEdgeMap.MultiFind(InNode, Edges);

		// Add parameters to collection based on edge description
		for (const FDataEdge* Edge : Edges)
		{
			if (!InDataReferenceMap.Contains(Edge->From.Node))
			{
				// TODO: Add to build error.
				// TODO: This is likely due to a failed topological sort and is more of an internal error
				// than a user error.
				return false;
			}

			
			const FDataReferenceCollection& FromDataReferenceCollection = InDataReferenceMap[Edge->From.Node].Outputs;

			if (!FromDataReferenceCollection.ContainsDataReadReference(Edge->From.Description.VertexName, Edge->From.Description.DataReferenceTypeName))
			{
				// TODO: This is likely a node programming error where the edges reported by the INode interface
				// did not match the readable parameter refs created by the operators outputs. Or, the edge description is invalid.
				// TODO: add build error to stack
				// TODO: consider checking that outputs of node operators match output descriptions of nodes. 
				// TODO: consider checking the edges are supported by nodes existing parameter descriptions.
				return false;
			}

			if (Edge->From.Description.DataReferenceTypeName != Edge->To.Description.DataReferenceTypeName)
			{
				// TODO: add build error.
				// TODO: may want to have this check higher up in the stack. 
				return false;
			}



			bool bSuccess = OutCollection.AddDataReadReferenceFrom(Edge->To.Description.VertexName, FromDataReferenceCollection, Edge->From.Description.VertexName, Edge->From.Description.DataReferenceTypeName);

			if (!bSuccess)
			{
				// TODO: add build error.
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
				// TODO Is this a separate error, or the stack of the same error. Perhaps this shouldn't add an error
				// because it's already sending the error downstream. 
				// TODO: add to build error
				return false;
			}

			IOperatorFactory& Factory = Node->GetDefaultOperatorFactory();

			FOperatorPtr Operator = Factory.CreateOperator(*Node, OperatorSettings, InputCollection, OutErrors);

			if (!Operator.IsValid())
			{
				// TODO: add vailed operator creation erorr. 
				// TODO Is this a separate error, or the stack of the same error. Perhaps this shouldn't add an error
				// because it's already sending the error downstream. 
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
		// Gather inputs
		for (const FInputDataVertex& InputVertex : InGraph.GetInputDataVertices())
		{
			if (!InNodeDataReferences.Contains(InputVertex.Node))
			{
				// TODO: build error.  input node does not exist.
				return false;
			}

			FDataReferenceCollection& Collection = InNodeDataReferences[InputVertex.Node].Inputs;

			if (!Collection.ContainsDataWriteReference(InputVertex.Description.VertexName, InputVertex.Description.DataReferenceTypeName))
			{
				// TODO: build error.  parameter ref does not exist
				return false;
			}

			bool bSuccess = OutGraphInputs.AddDataWriteReferenceFrom(InputVertex.Description.VertexName, Collection, InputVertex.Description.VertexName, InputVertex.Description.DataReferenceTypeName);

			if (!bSuccess)
			{
				// TODO: add build error
				// Shouldn't be hitting this error because of earlier "ContainsWritable..." call.
				return false;
			}
		}

		// Gather outputs
		for (const FOutputDataVertex& OutputVertex : InGraph.GetOutputDataVertices())
		{
			if (!InNodeDataReferences.Contains(OutputVertex.Node))
			{
				// TODO: build error.  input node does not exist.
				return false;
			}

			FDataReferenceCollection& Collection = InNodeDataReferences[OutputVertex.Node].Outputs;

			if (!Collection.ContainsDataReadReference(OutputVertex.Description.VertexName, OutputVertex.Description.DataReferenceTypeName))
			{
				// TODO: build error.  parameter ref does not exist
				return false;
			}

			bool bSuccess = OutGraphOutputs.AddDataReadReferenceFrom(OutputVertex.Description.VertexName, Collection, OutputVertex.Description.VertexName, OutputVertex.Description.DataReferenceTypeName);

			if (!bSuccess)
			{
				// TODO: add build error
				// Shouldn't be hitting this error because of earlier "ContainsWritable..." call.
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
			// TODO: add build error.
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

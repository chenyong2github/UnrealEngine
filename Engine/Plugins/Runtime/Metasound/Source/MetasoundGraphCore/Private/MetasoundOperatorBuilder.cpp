// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundOperatorBuilder.h"

#include "Algo/Count.h"
#include "Algo/Transform.h"

#include "MetasoundBuildError.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundBuildError.h"
#include "MetasoundGraphAlgo.h"
#include "MetasoundGraphOperator.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundGraphAlgo.h"
#include "MetasoundGraphLinter.h"
#include "Templates/PimplPtr.h"


namespace Metasound
{
	namespace OperatorBuilderPrivate
	{
		using FBuildErrorPtr = TUniquePtr<IOperatorBuildError>;

		// Convenience function for adding graph cycle build errors
		void AddBuildErrorsForCycles(const FDirectedGraphAlgoAdapter& InAdapter, TArray<FBuildErrorPtr>& OutErrors)
		{
			if (FGraphLinter::ValidateNoCyclesInGraph(InAdapter, OutErrors))
			{
				AddBuildError<FInternalError>(OutErrors, __FILE__, __LINE__);
			}
		}
	}

	FOperatorBuilder::~FOperatorBuilder()
	{
	}

	TUniquePtr<IOperator> FOperatorBuilder::BuildGraphOperator(const FBuildGraphParams& InParams, FBuildGraphResults& OutResults) const
	{
		using namespace OperatorBuilderPrivate;

		FBuildStatus BuildStatus = FBuildStatus::NoError;
		const FBuildStatus MaxBuildStatusErrorLevel = FBuildStatus::GetMaxErrorLevel(InParams.BuilderSettings);

		// Validate that the sources and destinations declared in an edge actually
		// exist in the node.
		if (InParams.BuilderSettings.bValidateVerticesExist)
		{
			if (!FGraphLinter::ValidateVerticesExist(InParams.Graph, OutResults.Errors))
			{
				BuildStatus |= FBuildStatus::FatalError;
			}
		}

		// Validate that the data types for a source and destination match.
		if (InParams.BuilderSettings.bValidateEdgeDataTypesMatch)
		{
			if (!FGraphLinter::ValidateEdgeDataTypesMatch(InParams.Graph, OutResults.Errors))
			{
				BuildStatus |= FBuildStatus::FatalError;
			}
		}
		
		// Validate that node inputs only have one source
		if (InParams.BuilderSettings.bValidateNoDuplicateInputs)
		{
			if (!FGraphLinter::ValidateNoDuplicateInputs(InParams.Graph, OutResults.Errors))
			{
				BuildStatus |= FBuildStatus::FatalError;
			}
		}

		// Possible early exit if edge validation fails.
		if (BuildStatus > MaxBuildStatusErrorLevel)
		{
			return TUniquePtr<IOperator>(nullptr);
		}

		// Create algo adapter view of graph to cache graph operations.
		TPimplPtr<FDirectedGraphAlgoAdapter> AlgoAdapter = FDirectedGraphAlgo::CreateDirectedGraphAlgoAdapter(InParams.Graph);
		
		if (!AlgoAdapter.IsValid())
		{
			AddBuildError<FInternalError>(OutResults.Errors, __FILE__, __LINE__);
			return TUniquePtr<IOperator>(nullptr);
		}

		FBuildContext BuildContext(InParams.Graph, *AlgoAdapter, InParams.OperatorSettings, InParams.Environment, InParams.BuilderSettings, OutResults);

		TArray<const INode*> SortedNodes;

		// Sort the nodes in a valid execution order
		BuildStatus |= DepthFirstTopologicalSort(BuildContext, SortedNodes);

		// TODO: Add FindReachableNodesFromVariables in Prune.
		// Otherwise, subgraphs incorrectly get pruned.
		// BuildStatus |= PruneNodges(BuildContext, SortedNodes);

		// Check build status in case build routine should be exited early.
		if (BuildStatus > MaxBuildStatusErrorLevel)
		{
			return TUniquePtr<IOperator>(nullptr);
		}

		// Create node operators from factories.
		BuildStatus |= CreateOperators(BuildContext, SortedNodes, InParams.InputDataReferences);

		if (BuildStatus > MaxBuildStatusErrorLevel)
		{
			return TUniquePtr<IOperator>(nullptr);
		}

		// Create graph operator from collection of node operators.
		return CreateGraphOperator(BuildContext, InParams);
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::DepthFirstTopologicalSort(FBuildContext& InOutContext, TArray<const INode*>& OutNodes) const
	{
		using namespace OperatorBuilderPrivate;

		bool bSuccess = FDirectedGraphAlgo::DepthFirstTopologicalSort(InOutContext.AlgoAdapter, OutNodes);

		if (!bSuccess)
		{
			// If there was an error, there is likely a cycle in the graph.
			AddBuildErrorsForCycles(InOutContext.AlgoAdapter, InOutContext.Results.Errors);

			return FBuildStatus::FatalError;
		}

		return FBuildStatus::NoError;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::KahnsTopologicalSort(FBuildContext& InOutContext, TArray<const INode*>& OutNodes) const
	{
		using namespace OperatorBuilderPrivate;

		bool bSuccess = FDirectedGraphAlgo::KahnTopologicalSort(InOutContext.AlgoAdapter, OutNodes);

		if (!bSuccess)
		{
			// If there was an error, there is likely a cycle in the graph.
			AddBuildErrorsForCycles(InOutContext.AlgoAdapter, InOutContext.Results.Errors);

			return FBuildStatus::FatalError;
		}

		return FBuildStatus::NoError;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::PruneNodes(FBuildContext& InOutContext, TArray<const INode*>& InOutNodes) const
	{
		using namespace OperatorBuilderPrivate;

		FBuildStatus BuildStatus = FBuildStatus::NoError;

		TSet<const INode*> ReachableNodes;

		switch (InOutContext.BuilderSettings.PruningMode)
		{
			case EOperatorBuilderNodePruning::PruneNodesWithoutExternalDependency:
				FDirectedGraphAlgo::FindReachableNodes(InOutContext.AlgoAdapter, ReachableNodes);
				break;

			case EOperatorBuilderNodePruning::PruneNodesWithoutOutputDependency:
				FDirectedGraphAlgo::FindReachableNodesFromOutput(InOutContext.AlgoAdapter, ReachableNodes);
				break;

			case EOperatorBuilderNodePruning::PruneNodesWithoutInputDependency:
				FDirectedGraphAlgo::FindReachableNodesFromInput(InOutContext.AlgoAdapter, ReachableNodes);
				break;

			case EOperatorBuilderNodePruning::None:
			default:
				return FBuildStatus::NoError;
		}

		if (InOutNodes.Num() == ReachableNodes.Num())
		{
			// Nothing to remove since all nodes are reachable. It's assumed that
			// InOutNodes has a unique set of nodes. 
			return FBuildStatus::NoError;
		}

		if (0 == ReachableNodes.Num())
		{
			// Pruning all nodes. 
			for (const INode* Node : InOutNodes)
			{
				AddBuildError<FNodePrunedError>(InOutContext.Results.Errors, Node);
			}

			InOutNodes.Reset();

			// This is non fatal, but results in an IOperator which is a No-op.
			return FBuildStatus::NonFatalError;
		}

		// Split the nodes-to-keep and the nodes-to-prune into two arrays. Need 
		// to ensure that kept nodes are still in same relative order. 
		TArray<const INode*> SortedNodesToKeep;

		SortedNodesToKeep.Reserve(ReachableNodes.Num());

		for (const INode* Node : InOutNodes)
		{
			if (ReachableNodes.Contains(Node))
			{
				SortedNodesToKeep.Add(Node);
			}
			else
			{
				AddBuildError<FNodePrunedError>(InOutContext.Results.Errors, Node);

				// Denote a pruned node as a non-fatal error. In the future this
				// may be simply a warning as some nodes are required to conform
				// to metasound interfaces even if they are unused.
				BuildStatus |= FBuildStatus::NonFatalError;
			}
		}

		InOutNodes = SortedNodesToKeep;

		return BuildStatus;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::GatherInputDataReferences(FBuildContext& InOutContext, const INode* InNode, const FNodeEdgeMultiMap& InEdgeMap, FDataReferenceCollection& OutInputCollection) const
	{
		using namespace OperatorBuilderPrivate;

		FBuildStatus BuildStatus;

		// Find all input edges associated with this node. 
		TArray<const FDataEdge*> Edges;
		InEdgeMap.MultiFind(InNode, Edges);

		// Add parameters to collection based on edge description
		for (const FDataEdge* Edge : Edges)
		{
			if (!InOutContext.DataReferences.Contains(Edge->From.Node))
			{
				// This is likely due to a failed topological sort and is more of an internal error
				// than a user error.
				AddBuildError<FInternalError>(InOutContext.Results.Errors, TEXT(__FILE__), __LINE__);

				return FBuildStatus::NonFatalError;
			}

			const FDataReferenceCollection& FromDataReferenceCollection = InOutContext.DataReferences[Edge->From.Node].Outputs;

			bool bSuccess = false;

			if (FromDataReferenceCollection.ContainsDataWriteReference(Edge->From.Vertex.VertexName, Edge->From.Vertex.DataTypeName))
			{
				// Contains data write reference
				bSuccess = OutInputCollection.AddDataWriteReferenceFrom(Edge->To.Vertex.VertexName, FromDataReferenceCollection, Edge->From.Vertex.VertexName, Edge->From.Vertex.DataTypeName);
			}
			else if (FromDataReferenceCollection.ContainsDataReadReference(Edge->From.Vertex.VertexName, Edge->From.Vertex.DataTypeName))
			{
				// Contains data read reference
				bSuccess = OutInputCollection.AddDataReadReferenceFrom(Edge->To.Vertex.VertexName, FromDataReferenceCollection, Edge->From.Vertex.VertexName, Edge->From.Vertex.DataTypeName);
			}

			if (!bSuccess)
			{
				// Does not contain any reference
				// This is likely a node programming error where the edges reported by the INode interface
				// did not match the readable parameter refs created by the operators outputs. Or, the edge description is invalid.

				// TODO: consider checking that outputs of node operators match output descriptions of nodes. 
				AddBuildError<FMissingOutputDataReferenceError>(InOutContext.Results.Errors, Edge->From);

				BuildStatus |= FBuildStatus::NonFatalError;
				continue;
			}
		}

		return BuildStatus;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::GatherExternalInputDataReferences(FBuildContext& InOutContext, const INode* InNode, const FNodeDestinationMap& InNodeDestinationMap, const FDataReferenceCollection& InExternalCollection, FDataReferenceCollection& OutDataReferences) const
	{
		using namespace OperatorBuilderPrivate;

		FBuildStatus BuildStatus;

		if (const FInputDataDestination* const* DestinationPtr = InNodeDestinationMap.Find(InNode))
		{
			if (const FInputDataDestination* Destination = *DestinationPtr)
			{
				const bool bReferenceExists = InExternalCollection.ContainsDataReadReference(Destination->Vertex.VertexName, Destination->Vertex.DataTypeName);
				if (bReferenceExists)
				{
					const bool bSuccess = OutDataReferences.AddDataReadReferenceFrom(Destination->Vertex.VertexName, InExternalCollection, Destination->Vertex.VertexName, Destination->Vertex.DataTypeName);

					if (!bSuccess)
					{
						AddBuildError<FInternalError>(InOutContext.Results.Errors, __FILE__, __LINE__);

						BuildStatus |= FBuildStatus::NonFatalError;
					}
				}
			}
		}

		return BuildStatus;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::CreateOperators(FBuildContext& InOutContext, const TArray<const INode*>& InSortedNodes, const FDataReferenceCollection& InExternalInputDataReferences) const
	{
		FBuildStatus BuildStatus;

		FNodeEdgeMultiMap NodeInputEdges;

		// Gather input edges for each node.
		for (const FDataEdge& Edge : InOutContext.Graph.GetDataEdges())
		{
			NodeInputEdges.Add(Edge.To.Node, &Edge);
		}

		// Map input nodes to graph destinations
		FNodeDestinationMap NodeDestinations;
		for (const auto& InputDestinationKV : InOutContext.Graph.GetInputDataDestinations())
		{
			const FInputDataDestination& Destination = InputDestinationKV.Value;
			NodeDestinations.Add(Destination.Node, &Destination);
		}

		// Call operator factory for each node.
		for (const INode* Node : InSortedNodes)
		{
			// Gather the input parameters for this IOperator from the output parameters of already created IOperators. 
			FDataReferenceCollection InputCollection;
			BuildStatus |= GatherInputDataReferences(InOutContext, Node, NodeInputEdges, InputCollection);

			// Gather the input parameters for this IOperator from the graph inputs.
			BuildStatus |= GatherExternalInputDataReferences(InOutContext, Node, NodeDestinations, InExternalInputDataReferences, InputCollection);

			if (BuildStatus >= FBuildStatus::FatalError)
			{
				return BuildStatus;
			}

			FOperatorFactorySharedRef Factory = Node->GetDefaultOperatorFactory();

			FCreateOperatorParams CreateParams { *Node, InOutContext.Settings, InputCollection, InOutContext.Environment, InOutContext.BuilderSettings };

			FOperatorPtr Operator = Factory->CreateOperator(CreateParams, InOutContext.Results);

			if (!Operator.IsValid())
			{
				return FBuildStatus::FatalError;
			}

			// Save input and output collection for future use 
			InOutContext.DataReferences.Emplace(Node, FOperatorDataReferences(Operator->GetInputs(), Operator->GetOutputs()));

			// Add operator to operator array
			InOutContext.Operators.Add(MoveTemp(Operator));
		}
		return BuildStatus;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::GatherGraphDataReferences(FBuildContext& InOutContext, FDataReferenceCollection& OutGraphInputs, FDataReferenceCollection& OutGraphOutputs) const
	{
		using namespace OperatorBuilderPrivate;
		using FDestinationElement = FInputDataDestinationCollection::ElementType;
		using FSourceElement = FOutputDataSourceCollection::ElementType;

		FBuildStatus BuildStatus;

		// Gather graph inputs
		for (const FDestinationElement& Element : InOutContext.Graph.GetInputDataDestinations())
		{
			const FInputDataDestination& InputDestination = Element.Value;

			if (!InOutContext.DataReferences.Contains(InputDestination.Node))
			{
				// An input node was likely pruned.
				AddBuildError<FMissingInputDataReferenceError>(InOutContext.Results.Errors, InputDestination);

				BuildStatus |= FBuildStatus::NonFatalError;
				continue;
			}

			const FDataReferenceCollection& Collection = InOutContext.DataReferences[InputDestination.Node].Inputs;

			// Get either readable or writable values for inputs. 
			// Readable inputs can occur when the data reference is provided outside
			// of the graph being built. 
			const bool bContainsWritableInput = Collection.ContainsDataWriteReference(InputDestination.Vertex.VertexName, InputDestination.Vertex.DataTypeName);
			const bool bContainsReadableInput = Collection.ContainsDataReadReference(InputDestination.Vertex.VertexName, InputDestination.Vertex.DataTypeName);

			const bool bContainsInput = bContainsWritableInput || bContainsReadableInput;

			if (!bContainsInput)
			{
				AddBuildError<FMissingInputDataReferenceError>(InOutContext.Results.Errors, InputDestination);

				BuildStatus |= FBuildStatus::NonFatalError;
				continue;
			}

			bool bSuccess = true;
			if (bContainsWritableInput)
			{
				bSuccess = OutGraphInputs.AddDataWriteReferenceFrom(InputDestination.Vertex.VertexName, Collection, InputDestination.Vertex.VertexName, InputDestination.Vertex.DataTypeName);
			}
			else
			{
				bSuccess = OutGraphInputs.AddDataReadReferenceFrom(InputDestination.Vertex.VertexName, Collection, InputDestination.Vertex.VertexName, InputDestination.Vertex.DataTypeName);
			}

			if (!bSuccess)
			{
				AddBuildError<FMissingInputDataReferenceError>(InOutContext.Results.Errors, InputDestination);

				BuildStatus |= FBuildStatus::NonFatalError;
				continue;
			}
		}

		// Gather graph outputs.
		for (const FSourceElement& Element : InOutContext.Graph.GetOutputDataSources())
		{
			const FOutputDataSource& OutputSource = Element.Value;

			if (!InOutContext.DataReferences.Contains(OutputSource.Node))
			{
				// An output node was likely pruned.
				AddBuildError<FMissingOutputDataReferenceError>(InOutContext.Results.Errors, OutputSource);

				BuildStatus |= FBuildStatus::NonFatalError;

				continue;
			}

			const FDataReferenceCollection& Collection = InOutContext.DataReferences[OutputSource.Node].Outputs;

			if (!Collection.ContainsDataReadReference(OutputSource.Vertex.VertexName, OutputSource.Vertex.DataTypeName))
			{
				// This will likely produce an IOperator which does not work as
				// expected.
				AddBuildError<FMissingOutputDataReferenceError>(InOutContext.Results.Errors, OutputSource);

				BuildStatus |= FBuildStatus::NonFatalError;

				continue;
			}

			bool bSuccess = OutGraphOutputs.AddDataReadReferenceFrom(OutputSource.Vertex.VertexName, Collection, OutputSource.Vertex.VertexName, OutputSource.Vertex.DataTypeName);

			if (!bSuccess)
			{
				// This will likely produce an IOperator which does not work as
				// expected.
				AddBuildError<FMissingOutputDataReferenceError>(InOutContext.Results.Errors, OutputSource);

				BuildStatus |= FBuildStatus::NonFatalError;

				continue;
			}
		}
	
		return BuildStatus;
	}

	TUniquePtr<IOperator> FOperatorBuilder::CreateGraphOperator(FBuildContext& InOutContext, const FBuildGraphParams& InParams) const
	{
		FDataReferenceCollection GraphInputs;
		FDataReferenceCollection GraphOutputs;

		FBuildStatus BuildStatus = GatherGraphDataReferences(InOutContext, GraphInputs, GraphOutputs);

		if (BuildStatus > FBuildStatus::GetMaxErrorLevel(InParams.BuilderSettings))
		{
			return TUniquePtr<IOperator>(nullptr);
		}

		TUniquePtr<FGraphOperator> GraphOperator = MakeUnique<FGraphOperator>();

		GraphOperator->SetInputs(GraphInputs);
		GraphOperator->SetOutputs(GraphOutputs);

		if (InOutContext.BuilderSettings.bPopulateInternalDataReferences)
		{
			TMap<FGuid, FDataReferenceCollection> References;
			for (TPair<const INode*, FOperatorDataReferences>& ReferencePair : InOutContext.DataReferences)
			{
				const INode* Node = ReferencePair.Key;
				check(Node);
				References.Emplace(Node->GetInstanceID(), MoveTemp(ReferencePair.Value.Outputs));
			}

			if (InParams.BuilderSettings.bPopulateInternalDataReferences)
			{
				InOutContext.Results.InternalDataReferences.Append(MoveTemp(References));
			}
		}

		for (FOperatorPtr& Ptr : InOutContext.Operators)
		{
			GraphOperator->AppendOperator(MoveTemp(Ptr));
		}

		return MoveTemp(GraphOperator);
	}
}

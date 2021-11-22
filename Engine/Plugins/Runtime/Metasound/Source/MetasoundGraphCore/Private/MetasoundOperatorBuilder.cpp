// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundOperatorBuilder.h"

#include "Algo/Count.h"
#include "Algo/Transform.h"
#include "CoreMinimal.h"

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

	// Default settings
	FOperatorBuilderSettings FOperatorBuilderSettings::GetDefaultSettings()
	{
#if UE_BUILD_SHIPPING
		return GetDefaultShippingSettings();
#elif UE_BUILD_TEST
		return GetDefaultTestSettings();
#elif UE_BUILD_DEVELOPMENT
		return GetDefaultDevelopementSettings();
#elif UE_BUILD_DEBUG
		return GetDefaultDebugSettings();
#else
		return GetDefaultShippingSettings();
#endif
	}

	// Debug settings
	FOperatorBuilderSettings FOperatorBuilderSettings::GetDefaultDebugSettings()
	{
		FOperatorBuilderSettings Settings;

		Settings.PruningMode = EOperatorBuilderNodePruning::None;
		Settings.bValidateNoCyclesInGraph = true;
		Settings.bValidateNoDuplicateInputs = true;
		Settings.bValidateVerticesExist = true;
		Settings.bValidateEdgeDataTypesMatch = true;
		Settings.bFailOnAnyError = false;

		return Settings;
	}

	// Development settings
	FOperatorBuilderSettings FOperatorBuilderSettings::GetDefaultDevelopementSettings()
	{
		FOperatorBuilderSettings Settings;

		Settings.PruningMode = EOperatorBuilderNodePruning::PruneNodesWithoutExternalDependency;
		Settings.bValidateNoCyclesInGraph = true;
		Settings.bValidateNoDuplicateInputs = true;
		Settings.bValidateVerticesExist = true;
		Settings.bValidateEdgeDataTypesMatch = true;
		Settings.bFailOnAnyError = false;

		return Settings;
	}

	// Testing settings
	FOperatorBuilderSettings FOperatorBuilderSettings::GetDefaultTestSettings()
	{
		FOperatorBuilderSettings Settings;

		Settings.PruningMode = EOperatorBuilderNodePruning::PruneNodesWithoutExternalDependency;
		Settings.bValidateNoCyclesInGraph = false;
		Settings.bValidateNoDuplicateInputs = false;
		Settings.bValidateVerticesExist = false;
		Settings.bValidateEdgeDataTypesMatch = false;
		Settings.bFailOnAnyError = false;

		return Settings;
	}

	// Shipping settings
	FOperatorBuilderSettings FOperatorBuilderSettings::GetDefaultShippingSettings()
	{
		FOperatorBuilderSettings Settings;

		Settings.PruningMode = EOperatorBuilderNodePruning::PruneNodesWithoutExternalDependency;
		Settings.bValidateNoCyclesInGraph = false;
		Settings.bValidateNoDuplicateInputs = false;
		Settings.bValidateVerticesExist = false;
		Settings.bValidateEdgeDataTypesMatch = false;
		Settings.bFailOnAnyError = false;

		return Settings;
	}


	FOperatorBuilder::FOperatorBuilder(const FOperatorBuilderSettings& InBuilderSettings)
	:	BuilderSettings(InBuilderSettings)
	{
		MaxBuildStatusErrorLevel = BuilderSettings.bFailOnAnyError ? FBuildStatus::NoError : FBuildStatus::NonFatalError;
	}

	FOperatorBuilder::~FOperatorBuilder()
	{
	}

	TUniquePtr<IOperator> FOperatorBuilder::BuildGraphOperator(const FBuildGraphParams& InParams, FBuildErrorArray& OutErrors) const
	{
		using namespace OperatorBuilderPrivate;

		FBuildStatus BuildStatus = FBuildStatus::NoError;

		// Validate that the sources and destinations declared in an edge actually
		// exist in the node.
		if (BuilderSettings.bValidateVerticesExist)
		{
			if (!FGraphLinter::ValidateVerticesExist(InParams.Graph, OutErrors))
			{
				BuildStatus |= FBuildStatus::FatalError;
			}
		}

		// Validate that the data types for a source and destination match.
		if (BuilderSettings.bValidateEdgeDataTypesMatch)
		{
			if (!FGraphLinter::ValidateEdgeDataTypesMatch(InParams.Graph, OutErrors))
			{
				BuildStatus |= FBuildStatus::FatalError;
			}
		}
		
		// Validate that node inputs only have one source
		if (BuilderSettings.bValidateNoDuplicateInputs)
		{
			if (!FGraphLinter::ValidateNoDuplicateInputs(InParams.Graph, OutErrors))
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
			AddBuildError<FInternalError>(OutErrors, __FILE__, __LINE__);
			return TUniquePtr<IOperator>(nullptr);
		}

		FBuildContext BuildContext(InParams.Graph, *AlgoAdapter, InParams.OperatorSettings, InParams.Environment, OutErrors);

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

		TArray<TUniquePtr<IOperator>> Operators;
		FNodeDataReferenceMap NodeData;

		// Create node operators from factories. 
		BuildStatus |= CreateOperators(BuildContext, SortedNodes, InParams.InputDataReferences, Operators, NodeData);

		if (BuildStatus > MaxBuildStatusErrorLevel)
		{
			return TUniquePtr<IOperator>(nullptr);
		}

		// Create graph operator from collection of node operators.
		return CreateGraphOperator(BuildContext, Operators, NodeData);
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::DepthFirstTopologicalSort(FBuildContext& InContext, TArray<const INode*>& OutNodes) const
	{
		using namespace OperatorBuilderPrivate;

		bool bSuccess = FDirectedGraphAlgo::DepthFirstTopologicalSort(InContext.AlgoAdapter, OutNodes);

		if (!bSuccess)
		{
			// If there was an error, there is likely a cycle in the graph.
			AddBuildErrorsForCycles(InContext.AlgoAdapter, InContext.Errors);

			return FBuildStatus::FatalError;
		}

		return FBuildStatus::NoError;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::KahnsTopologicalSort(FBuildContext& InContext, TArray<const INode*>& OutNodes) const
	{
		using namespace OperatorBuilderPrivate;

		bool bSuccess = FDirectedGraphAlgo::KahnTopologicalSort(InContext.AlgoAdapter, OutNodes);

		if (!bSuccess)
		{
			// If there was an error, there is likely a cycle in the graph.
			AddBuildErrorsForCycles(InContext.AlgoAdapter, InContext.Errors);

			return FBuildStatus::FatalError;
		}

		return FBuildStatus::NoError;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::PruneNodes(FBuildContext& InContext, TArray<const INode*>& InOutNodes) const
	{
		using namespace OperatorBuilderPrivate;

		FBuildStatus BuildStatus = FBuildStatus::NoError;

		TSet<const INode*> ReachableNodes;

		switch (BuilderSettings.PruningMode)
		{
			case EOperatorBuilderNodePruning::PruneNodesWithoutExternalDependency:
				FDirectedGraphAlgo::FindReachableNodes(InContext.AlgoAdapter, ReachableNodes);
				break;

			case EOperatorBuilderNodePruning::PruneNodesWithoutOutputDependency:
				FDirectedGraphAlgo::FindReachableNodesFromOutput(InContext.AlgoAdapter, ReachableNodes);
				break;

			case EOperatorBuilderNodePruning::PruneNodesWithoutInputDependency:
				FDirectedGraphAlgo::FindReachableNodesFromInput(InContext.AlgoAdapter, ReachableNodes);
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
				AddBuildError<FNodePrunedError>(InContext.Errors, Node);
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
				AddBuildError<FNodePrunedError>(InContext.Errors, Node);

				// Denote a pruned node as a non-fatal error. In the future this
				// may be simply a warning as some nodes are required to conform
				// to metasound interfaces even if they are unused.
				BuildStatus |= FBuildStatus::NonFatalError;
			}
		}

		InOutNodes = SortedNodesToKeep;

		return BuildStatus;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::GatherInputDataReferences(FBuildContext& InContext, const INode* InNode, const FNodeEdgeMultiMap& InEdgeMap, const FNodeDataReferenceMap& InDataReferenceMap, FDataReferenceCollection& OutCollection) const
	{
		using namespace OperatorBuilderPrivate;

		FBuildStatus BuildStatus;

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
				AddBuildError<FInternalError>(InContext.Errors, TEXT(__FILE__), __LINE__);

				return FBuildStatus::NonFatalError;
			}

			const FDataReferenceCollection& FromDataReferenceCollection = InDataReferenceMap[Edge->From.Node].Outputs;

			bool bSuccess = false;

			if (FromDataReferenceCollection.ContainsDataWriteReference(Edge->From.Vertex.GetVertexName(), Edge->From.Vertex.GetDataTypeName()))
			{
				// Contains data write reference
				bSuccess = OutCollection.AddDataWriteReferenceFrom(Edge->To.Vertex.GetVertexName(), FromDataReferenceCollection, Edge->From.Vertex.GetVertexName(), Edge->From.Vertex.GetDataTypeName());
			}
			else if (FromDataReferenceCollection.ContainsDataReadReference(Edge->From.Vertex.GetVertexName(), Edge->From.Vertex.GetDataTypeName()))
			{
				// Contains data read reference
				bSuccess = OutCollection.AddDataReadReferenceFrom(Edge->To.Vertex.GetVertexName(), FromDataReferenceCollection, Edge->From.Vertex.GetVertexName(), Edge->From.Vertex.GetDataTypeName());
			}

			if (!bSuccess)
			{
				// Does not contain any reference
				// This is likely a node programming error where the edges reported by the INode interface
				// did not match the readable parameter refs created by the operators outputs. Or, the edge description is invalid.

				// TODO: consider checking that outputs of node operators match output descriptions of nodes. 
				AddBuildError<FMissingOutputDataReferenceError>(InContext.Errors, Edge->From);

				BuildStatus |= FBuildStatus::NonFatalError;
				continue;
			}
		}

		return BuildStatus;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::GatherExternalInputDataReferences(FBuildContext& InContext, const INode* InNode, const FNodeDestinationMap& InNodeDestinationMap, const FDataReferenceCollection& InExternalCollection, FDataReferenceCollection& OutDataReferences) const
	{
		using namespace OperatorBuilderPrivate;

		FBuildStatus BuildStatus;

		if (const FInputDataDestination* const* DestinationPtr = InNodeDestinationMap.Find(InNode))
		{
			if (const FInputDataDestination* Destination = *DestinationPtr)
			{
				const bool bReferenceExists = InExternalCollection.ContainsDataReadReference(Destination->Vertex.GetVertexName(), Destination->Vertex.GetDataTypeName());
				if (bReferenceExists)
				{
					const bool bSuccess = OutDataReferences.AddDataReadReferenceFrom(Destination->Vertex.GetVertexName(), InExternalCollection, Destination->Vertex.GetVertexName(), Destination->Vertex.GetDataTypeName());

					if (!bSuccess)
					{
						AddBuildError<FInternalError>(InContext.Errors, __FILE__, __LINE__);

						BuildStatus |= FBuildStatus::NonFatalError;
					}
				}
			}
		}

		return BuildStatus;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::CreateOperators(FBuildContext& InContext, const TArray<const INode*>& InSortedNodes, const FDataReferenceCollection& InExternalInputDataReferences, TArray<TUniquePtr<IOperator>>& OutOperators, FNodeDataReferenceMap& OutDataReferences) const
	{
		FBuildStatus BuildStatus;

		FNodeEdgeMultiMap NodeInputEdges;

		// Gather input edges for each node.
		for (const FDataEdge& Edge : InContext.Graph.GetDataEdges())
		{
			NodeInputEdges.Add(Edge.To.Node, &Edge);
		}

		// Map input nodes to graph destinations
		FNodeDestinationMap NodeDestinations;
		for (const auto& InputDestinationKV : InContext.Graph.GetInputDataDestinations())
		{
			const FInputDataDestination& Destination = InputDestinationKV.Value;
			NodeDestinations.Add(Destination.Node, &Destination);
		}

		// Call operator factory for each node.
		for (const INode* Node : InSortedNodes)
		{
			FDataReferenceCollection InputCollection;

			// Gather the input parameters for this IOperator from the output parameters of already created IOperators. 
			BuildStatus |= GatherInputDataReferences(InContext, Node, NodeInputEdges, OutDataReferences, InputCollection);
			// Gather the input parameters for this IOperator from the graph inputs.
			BuildStatus |= GatherExternalInputDataReferences(InContext, Node, NodeDestinations, InExternalInputDataReferences, InputCollection);

			if (BuildStatus >= FBuildStatus::FatalError)
			{
				return BuildStatus;
			}

			FOperatorFactorySharedRef Factory = Node->GetDefaultOperatorFactory();

			FCreateOperatorParams CreateParams{*Node, InContext.Settings, InputCollection, InContext.Environment, this};

			FOperatorPtr Operator = Factory->CreateOperator(CreateParams, InContext.Errors);

			if (!Operator.IsValid())
			{
				return FBuildStatus::FatalError;
			}

			// Save input and output collection for future use 
			OutDataReferences.Emplace(Node, FOperatorDataReferences(Operator->GetInputs(), Operator->GetOutputs()));

			// Add operator to operator array
			OutOperators.Add(MoveTemp(Operator));
		}

		return BuildStatus;
	}

	FOperatorBuilder::FBuildStatus FOperatorBuilder::GatherGraphDataReferences(FBuildContext& InContext, FNodeDataReferenceMap& InNodeDataReferences, FDataReferenceCollection& OutGraphInputs, FDataReferenceCollection& OutGraphOutputs) const
	{
		using namespace OperatorBuilderPrivate;
		using FDestinationElement = FInputDataDestinationCollection::ElementType;
		using FSourceElement = FOutputDataSourceCollection::ElementType;

		FBuildStatus BuildStatus;

		// Gather graph inputs
		for (const FDestinationElement& Element : InContext.Graph.GetInputDataDestinations())
		{
			const FInputDataDestination& InputDestination = Element.Value;

			if (!InNodeDataReferences.Contains(InputDestination.Node))
			{
				// An input node was likely pruned.
				AddBuildError<FMissingInputDataReferenceError>(InContext.Errors, InputDestination);

				BuildStatus |= FBuildStatus::NonFatalError;
				continue;
			}

			FDataReferenceCollection& Collection = InNodeDataReferences[InputDestination.Node].Inputs;

			// Get either readable or writable values for inputs. 
			// Readable inputs can occur when the data reference is provided outside
			// of the graph being built. 
			const bool bContainsWritableInput = Collection.ContainsDataWriteReference(InputDestination.Vertex.GetVertexName(), InputDestination.Vertex.GetDataTypeName());
			const bool bContainsReadableInput = Collection.ContainsDataReadReference(InputDestination.Vertex.GetVertexName(), InputDestination.Vertex.GetDataTypeName());

			const bool bContainsInput = bContainsWritableInput || bContainsReadableInput;

			if (!bContainsInput)
			{
				AddBuildError<FMissingInputDataReferenceError>(InContext.Errors, InputDestination);

				BuildStatus |= FBuildStatus::NonFatalError;
				continue;
			}

			bool bSuccess = true;
			if (bContainsWritableInput)
			{
				bSuccess = OutGraphInputs.AddDataWriteReferenceFrom(InputDestination.Vertex.GetVertexName(), Collection, InputDestination.Vertex.GetVertexName(), InputDestination.Vertex.GetDataTypeName());
			}
			else
			{
				bSuccess = OutGraphInputs.AddDataReadReferenceFrom(InputDestination.Vertex.GetVertexName(), Collection, InputDestination.Vertex.GetVertexName(), InputDestination.Vertex.GetDataTypeName());
			}

			if (!bSuccess)
			{
				AddBuildError<FMissingInputDataReferenceError>(InContext.Errors, InputDestination);

				BuildStatus |= FBuildStatus::NonFatalError;
				continue;
			}
		}

		// Gather graph outputs.
		for (const FSourceElement& Element : InContext.Graph.GetOutputDataSources())
		{
			const FOutputDataSource& OutputSource = Element.Value;

			if (!InNodeDataReferences.Contains(OutputSource.Node))
			{
				// An output node was likely pruned.
				AddBuildError<FMissingOutputDataReferenceError>(InContext.Errors, OutputSource);

				BuildStatus |= FBuildStatus::NonFatalError;

				continue;
			}

			FDataReferenceCollection& Collection = InNodeDataReferences[OutputSource.Node].Outputs;

			if (!Collection.ContainsDataReadReference(OutputSource.Vertex.GetVertexName(), OutputSource.Vertex.GetDataTypeName()))
			{
				// This will likely produce an IOperator which does not work as
				// expected.
				AddBuildError<FMissingOutputDataReferenceError>(InContext.Errors, OutputSource);

				BuildStatus |= FBuildStatus::NonFatalError;

				continue;
			}

			bool bSuccess = OutGraphOutputs.AddDataReadReferenceFrom(OutputSource.Vertex.GetVertexName(), Collection, OutputSource.Vertex.GetVertexName(), OutputSource.Vertex.GetDataTypeName());

			if (!bSuccess)
			{
				// This will likely produce an IOperator which does not work as
				// expected.
				AddBuildError<FMissingOutputDataReferenceError>(InContext.Errors, OutputSource);

				BuildStatus |= FBuildStatus::NonFatalError;

				continue;
			}
		}
	
		return BuildStatus;
	}

	TUniquePtr<IOperator> FOperatorBuilder::CreateGraphOperator(FBuildContext& InContext, TArray<FOperatorPtr>& InOperators, FNodeDataReferenceMap& InNodeDataReferences) const
	{
		FDataReferenceCollection GraphInputs;
		FDataReferenceCollection GraphOutputs;

		FBuildStatus BuildStatus = GatherGraphDataReferences(InContext, InNodeDataReferences, GraphInputs, GraphOutputs);

		if (BuildStatus > MaxBuildStatusErrorLevel)
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

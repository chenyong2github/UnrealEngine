// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundBuilderInterface.h"
#include "MetasoundEnvironment.h"
#include "MetasoundGraphOperator.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorBuilderSettings.h"
#include "MetasoundOperatorInterface.h"

namespace Metasound
{
	// Forward declare.
	class FDirectedGraphAlgoAdapter;

	/** FOperatorBuilder builds an IOperator from an IGraph. */
	class METASOUNDGRAPHCORE_API FOperatorBuilder : public IOperatorBuilder
	{
		public:

			/** FOperatorBuilder constructor.
			 *
			 * @param InBuilderSettings  - Settings to configure builder options.
			 */
			FOperatorBuilder() = default;

			virtual ~FOperatorBuilder();

			/** Create an IOperator from an IGraph.
			 *
			 * @param InParams   - Params of the current build
			 * @param OutResults - Results data pertaining to the given build operator result.
			 *
			 * @return A TUniquePtr to an IOperator. If the processes was unsuccessful, 
			 *         the returned pointer will contain a nullptr and be invalid.
			 */
			virtual TUniquePtr<IOperator> BuildGraphOperator(const FBuildGraphParams& InParams, FBuildGraphResults& OutResults) const override;

		private:
			// Collection of existing inputs and outputs associated with a given
			// IOperator.
			struct FOperatorDataReferences
			{
				FOperatorDataReferences()
				{
				}

				FOperatorDataReferences(const FDataReferenceCollection& InInputs, const FDataReferenceCollection& InOutputs)
				:	Inputs(InInputs)
				,	Outputs(InOutputs)
				{
				}

				FDataReferenceCollection Inputs;
				FDataReferenceCollection Outputs;
			};

			using FNodeEdgeMultiMap = TMultiMap<const INode*, const FDataEdge*>;
			using FNodeDestinationMap = TMap<const INode*, const FInputDataDestination*>;
			using FNodeDataReferenceMap = TMap<const INode*, FOperatorDataReferences>;
			using FOperatorPtr = TUniquePtr<IOperator>;

			// Handles build status of current build operation.
			struct FBuildStatus
			{
				// Enumeration of build status states. 
				//
				// Note: plain enum used here instead of enum class so that implicit 
				// conversion to int32 can be utilized. It is assumed that the 
				// build status int32 values increase as the build status deteriorates.
				// Build statuses are merged by taking the maximum int32 value of
				// the EStatus. 
				enum EStatus
				{
					// No error has been encountered.
					NoError = 0,
					
					// A non fatal error has been encountered.
					NonFatalError = 1,

					// A fatal error has been encountered.
					FatalError = 2
				};

				static FBuildStatus GetMaxErrorLevel(const FOperatorBuilderSettings& InBuilderSettings)
				{
					return InBuilderSettings.bFailOnAnyError ? EStatus::NoError : EStatus::NonFatalError;
				}

				FBuildStatus() = default;

				FBuildStatus(FBuildStatus::EStatus InStatus)
				:	Value(InStatus)
				{
				}

				// Merge build statuses by taking the maximum of EStatus.
				FBuildStatus& operator |= (FBuildStatus RHS)
				{
					Value = Value > RHS.Value ? Value : RHS.Value;
					return *this;
				}

				operator EStatus() const
				{
					return Value;
				}

			private:
				EStatus Value = NoError;
			};

			struct FBuildContext
			{
				const IGraph& Graph;
				const FDirectedGraphAlgoAdapter& AlgoAdapter;
				const FOperatorSettings& Settings;
				const FMetasoundEnvironment& Environment;
				const FOperatorBuilderSettings& BuilderSettings;

				FBuildGraphResults& Results;

				TArray<FOperatorPtr> Operators;
				FNodeDataReferenceMap DataReferences;

				FBuildContext(
					const IGraph& InGraph,
					const FDirectedGraphAlgoAdapter& InAlgoAdapter,
					const FOperatorSettings& InSettings,
					const FMetasoundEnvironment& InEnvironment,
					const FOperatorBuilderSettings& bInBuilderSettings,
					FBuildGraphResults& OutResults)
				:	Graph(InGraph)
				,	AlgoAdapter(InAlgoAdapter)
				,	Settings(InSettings)
				,	Environment(InEnvironment)
				,	BuilderSettings(bInBuilderSettings)
				,	Results(OutResults)
				{
				}
			};

			// Perform topological sort using depth first algorithm.
			FBuildStatus DepthFirstTopologicalSort(FBuildContext& InOutContext, TArray<const INode*>& OutNodes) const;

			// Perform topological sort using kahns algorithm.
			FBuildStatus KahnsTopologicalSort(FBuildContext& InOutContext, TArray<const INode*>& OutNodes) const;

			// Prune unreachable nodes from InOutNodes
			FBuildStatus PruneNodes(FBuildContext& InOutContext, TArray<const INode*>& InOutNodes) const;

			// Get all input data references for a given node for inputs provided internally to the graph.
			FBuildStatus GatherInputDataReferences(FBuildContext& InOutContext, const INode* InNode, const FNodeEdgeMultiMap& InEdgeMap, FDataReferenceCollection& OutInputCollection) const;

			// Get all input data references for a given node for inputs provided externally to the graph.
			FBuildStatus GatherExternalInputDataReferences(FBuildContext& InOutContext, const INode* InNode, const FNodeDestinationMap& InNodeDestinationMap, const FDataReferenceCollection& InExternalCollection, FDataReferenceCollection& OutDataReferences) const;

			// Get all input/output data references for a given graph.
			FBuildStatus GatherGraphDataReferences(FBuildContext& InOutContext, FDataReferenceCollection& OutGraphInputs, FDataReferenceCollection& OutGraphOutputs) const;

			// Call the operator factories for the nodes
			FBuildStatus CreateOperators(FBuildContext& InOutContext, const TArray<const INode*>& InSortedNodes, const FDataReferenceCollection& InGraphInputs) const;

			// Create the final graph operator from the provided build context.
			TUniquePtr<IOperator> CreateGraphOperator(FBuildContext& InOutContext, const FBuildGraphParams& InParams) const;
	};
}


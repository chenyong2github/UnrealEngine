// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundEnvironment.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundBuilderInterface.h"

namespace Metasound
{
	/** EOperatorBuildNodePruning expresses the desired pruning behavior during
	 * the node pruning step.  
	 *
	 * Some nodes are unreachable in the graph either by traversing from the
	 * input nodes to from the output nodes. Because they are not dependent, 
	 * they have no impact on the produced output and can be pruned without 
	 * causing any change to the declared behavior of the graph.
	 */
	enum class EOperatorBuilderNodePruning : uint8
	{
		/** Do not prune any nodes. */
		None, 

		/** Prune nodes which cannot be reached from the output nodes. */
		PruneNodesWithoutOutputDependency, 

		/** Prune nodes which cannot be reached from the input nodes. */
		PruneNodesWithoutInputDependency, 

		/** Prune nodes which cannot be reached from the input nodes or output nodes. */
		PruneNodesWithoutExternalDependency, 
	};

	/** FOperatorBuilderSettings
	 *
	 * Settings for building IGraphs into IOperators.
	 */
	struct METASOUNDGRAPHCORE_API FOperatorBuilderSettings
	{
		/** Desired node pruning behavior. */
		EOperatorBuilderNodePruning PruningMode = EOperatorBuilderNodePruning::None;

		/** If true, the IGraph will be analyzed to detect cycles. Errors will be
		 * generated if a cycle is detected in the graph.
		 */
		bool bValidateNoCyclesInGraph = true;

		/** If true, the inputs to each node in the IGraph will be analyzed to
		 * detect duplicate inputs connected to an individual vertex on a given 
		 * node.  Errors will be generated if duplicates are detected. */
		bool bValidateNoDuplicateInputs = true;

		/** If true, each FDataEdge in the IGraph will be validated by checking
		 * that the corresponding INodes contain matching FDataVertex information
		 * as described by the FDataEdge. Errors will be generated if 
		 * inconsistencies are detected.
		 */
		bool bValidateVerticesExist = true;

		/** If true, each FDataEdge in the IGraph will be validated by checking
		 * that the FInputDataSource and FOutputDataDestination data types are
		 * equal. Errors will be generated if unequal data types are detected.
		 */
		bool bValidateEdgeDataTypesMatch = true;

		/** If true, the builder will return an invalid IOperator if any errors
		 * are detected. If false, the builder will return an invalid IOperator
		 * only if fatal errors are detected.
		 */
		bool bFailOnAnyError = false;

		/** Return the default settings for the current build environment. */
		static FOperatorBuilderSettings GetDefaultSettings();

		/** Return the default settings for a debug build environment. */
		static FOperatorBuilderSettings GetDefaultDebugSettings();

		/** Return the default settings for a development build environment. */
		static FOperatorBuilderSettings GetDefaultDevelopementSettings();

		/** Return the default settings for a test build environment. */
		static FOperatorBuilderSettings GetDefaultTestSettings();

		/** Return the default settings for a shipping build environment. */
		static FOperatorBuilderSettings GetDefaultShippingSettings();
	};

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
			FOperatorBuilder(const FOperatorBuilderSettings& InBuilderSettings);

			virtual ~FOperatorBuilder();

			/** Create an IOperator from an IGraph.
			 *
			 * @param InGraph            - The graph containing input, output and edge information.
			 * @param InOperatorSettings - Settings to be passed to all operators on creation.
			 * @param InEnvironment      - The environment variables to use during construction. 
			 * @param OutErrors          - An array of build errors that will be populated with any issues encountered during the build process.
			 *
			 * @return A TUniquePtr to an IOperator. If the processes was unsuccessful, 
			 *         the returned pointer will contain a nullptr and be invalid.
			 */
			virtual TUniquePtr<IOperator> BuildGraphOperator(const IGraph& InGraph, const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment, TArray<FBuildErrorPtr>& OutErrors) override;
			

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

				TArray<FBuildErrorPtr>& Errors;

				FBuildContext(const IGraph& InGraph, FDirectedGraphAlgoAdapter& InAlgoAdapter, const FOperatorSettings& InSettings, const FMetasoundEnvironment& InEnvironment, TArray<FBuildErrorPtr>& InOutErrors)
				:	Graph(InGraph)
				,	AlgoAdapter(InAlgoAdapter)
				,	Settings(InSettings)
				,	Environment(InEnvironment)
				,	Errors(InOutErrors)
				{
				}
			};

			// Perform topological sort using depth first algorithm.
			FBuildStatus DepthFirstTopologicalSort(FBuildContext& InContext, TArray<const INode*>& OutNodes) const;

			// Perform topological sort using kahns algorithm.
			FBuildStatus KahnsTopologicalSort(FBuildContext& InContext, TArray<const INode*>& OutNodes) const;

			// Prune unreachable nodes from InOutNodes
			FBuildStatus PruneNodes(FBuildContext& InContext, TArray<const INode*>& InOutNodes) const;

			// Get all input data references for a given node.
			FBuildStatus GatherInputDataReferences(FBuildContext& InContext, const INode* InNode, const FNodeEdgeMultiMap& InEdgeMap, const FNodeDataReferenceMap& InDataReferenceMap, FDataReferenceCollection& OutCollection) const;

			// Get all input/output data references for a given graph.
			FBuildStatus GatherGraphDataReferences(FBuildContext& InContext, FNodeDataReferenceMap& InNodeDataReferences, FDataReferenceCollection& OutGraphInputs, FDataReferenceCollection& OutGraphOutputs) const;

			// Call the operator factories for the nodes
			FBuildStatus CreateOperators(FBuildContext& InContext, const TArray<const INode*>& InSortedNodes, TArray<FOperatorPtr>& OutOperators, FNodeDataReferenceMap& OutDataReferences) const;

			// Create the final graph operator from the individual operators.
			TUniquePtr<IOperator> CreateGraphOperator(FBuildContext& InContext, TArray<FOperatorPtr>& InOperators, FNodeDataReferenceMap& InNodeDataReferences) const;

			FOperatorBuilderSettings BuilderSettings;

			FBuildStatus MaxBuildStatusErrorLevel;
	};
}


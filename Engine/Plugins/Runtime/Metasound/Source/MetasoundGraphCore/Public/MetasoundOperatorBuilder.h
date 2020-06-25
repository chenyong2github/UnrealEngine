// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundBuilderInterface.h"

namespace Metasound
{
	class METASOUNDGRAPHCORE_API FOperatorBuilder : public IOperatorBuilder
	{
		public:
			FOperatorBuilder(const FOperatorSettings& InSettings);

			virtual ~FOperatorBuilder();

			virtual TUniquePtr<IOperator> BuildGraphOperator(const IGraph& InGraph, TArray<FBuildErrorPtr>& OutErrors) override;
			

		private:
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
			using FNodeDataReferenceMap = TMap<INode*, FOperatorDataReferences>;
			using FOperatorPtr = TUniquePtr<IOperator>;

			bool GroupInputEdges(const TArray<FDataEdge>& InEdges, FNodeEdgeMultiMap& OutNodeInputs, TArray<FBuildErrorPtr>& OutErrors) const;

			bool TopologicalSort(const TArray<FDataEdge>& InEdges, TArray<INode*>& OutNodes, TArray<FBuildErrorPtr>& OutErrors) const;

			bool GatherInputDataReferences(const INode* InNode, const FNodeEdgeMultiMap& InEdgeMap, const FNodeDataReferenceMap& InDataReferenceMap, FDataReferenceCollection& OutCollection, TArray<FBuildErrorPtr>& OutErrors) const;

			bool GatherGraphDataReferences(const IGraph& InGraph, FNodeDataReferenceMap& InNodeDataReferences, FDataReferenceCollection& OutGraphInputs, FDataReferenceCollection& OutGraphOutputs, TArray<FBuildErrorPtr>& OutErrors) const;

			bool CreateOperators(const TArray<INode*>& InSortedNodes, FNodeEdgeMultiMap& InNodeInputEdges, TArray<FOperatorPtr>& OutOperators, FNodeDataReferenceMap& OutDataReferences, TArray<FBuildErrorPtr>& OutErrors);

			TUniquePtr<IOperator> CreateGraphOperator(const IGraph& InGraph, TArray<FOperatorPtr>& InOperators, FNodeDataReferenceMap& InNodeDataReferences, TArray<FBuildErrorPtr>& OutErrors) const;

			FOperatorSettings OperatorSettings;
	};
}


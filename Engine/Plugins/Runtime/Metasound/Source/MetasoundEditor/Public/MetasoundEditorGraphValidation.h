// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Algo/NoneOf.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphNode.h"

class UMetasoundEditorGraphNode;

namespace Metasound
{
	namespace Editor
	{
		struct METASOUNDEDITOR_API FGraphNodeValidationResult
		{
			// Node associated with validation result
			UMetasoundEditorGraphNode* Node = nullptr;

			// Whether associated node is in invalid state (document is corrupt and no Frontend representation could be
			// found for the node)
			bool bIsInvalid = false;

			// Whether validation made changes to the node and is now in a dirty state
			bool bIsDirty = false;

			FGraphNodeValidationResult(UMetasoundEditorGraphNode& InNode)
				: Node(&InNode)
			{
			}
		};

		struct METASOUNDEDITOR_API FGraphValidationResults
		{
			TArray<FGraphNodeValidationResult> NodeResults;

			// Results corresponding with node validation
			const TArray<FGraphNodeValidationResult>& GetResults() const
			{
				return NodeResults;
			}

			// Returns whether or not the graph is in a valid state
			bool IsValid() const
			{
				return Algo::NoneOf(NodeResults, [](const FGraphNodeValidationResult& Result)
				{
					return Result.bIsInvalid;
				});
			}
		};
	} // namespace Editor
} // namespace Metasound

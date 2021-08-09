// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphNode.h"


namespace Metasound
{
	namespace Editor
	{
		struct METASOUNDEDITOR_API FGraphNodeValidationResult
		{
			// Node associated with validation result
			UMetasoundEditorGraphExternalNode* Node = nullptr;

			// Whether associated node is in invalid state (document is corrupt and no Frontend representation could be
			// found for the node)
			bool bIsInvalid = false;

			// Whether validation made changes to the node and is now in a dirty state
			bool bIsDirty = false;

			FGraphNodeValidationResult(UMetasoundEditorGraphExternalNode& InNode)
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
				auto NodeIsInvalid = [](const FGraphNodeValidationResult& Result)
				{
					return Result.bIsInvalid;
				};
				return !NodeResults.ContainsByPredicate(NodeIsInvalid);
			}
		};
	} // namespace Editor
} // namespace Metasound

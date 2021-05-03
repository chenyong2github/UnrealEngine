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

			// If not none, name of GraphNode's missing (not found in Frontend Registry) class as described in the owning
			// Frontend document model.
			FName MissingClass;

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

			// Returns set of missing class names from the validation results
			TSet<FName> FindMissingClasses() const
			{
				TSet<FName> MissingClasses;
				for (const FGraphNodeValidationResult& Result : NodeResults)
				{
					if (!Result.MissingClass.IsNone())
					{
						MissingClasses.Add(Result.MissingClass);
					}
				}

				return MissingClasses;
			}
		};
	} // namespace Editor
} // namespace Metasound

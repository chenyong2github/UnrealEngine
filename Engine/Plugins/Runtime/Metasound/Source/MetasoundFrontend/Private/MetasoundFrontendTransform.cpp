// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendTransform.h"

namespace Metasound
{
	namespace Frontend
	{
		bool FMatchRootGraphToArchetype::Transform(FDocumentHandle InDocument) const
		{
			if (!InDocument->IsValid())
			{
				return false;
			}

			bool bDidEdit = false;
			FGraphHandle Graph = InDocument->GetRootGraph();

			TArray<FMetasoundFrontendClassVertex> RequiredInputs = InDocument->GetRequiredInputs();
			TArray<FMetasoundFrontendClassVertex> RequiredOutputs = InDocument->GetRequiredOutputs();

			// Go through each input and add or swap if something is missing.
			for (const FMetasoundFrontendClassVertex& RequiredInput : RequiredInputs)
			{
				const FMetasoundFrontendClassInput* ClassInput = Graph->FindClassInputWithName(RequiredInput.Name).Get();

				bool bIsMatching = false;
				if (nullptr != ClassInput)
				{
					bIsMatching = FMetasoundFrontendClassVertex::IsFunctionalEquivalent(RequiredInput, *ClassInput);
				}

				if (!bIsMatching)
				{
					bDidEdit = true;
					if (nullptr != ClassInput)
					{
						Graph->RemoveInputVertex(RequiredInput.Name);
					}
					Graph->AddInputVertex(RequiredInput);
				}
			}

			// Go through each output and add or swap if something is missing.
			for (const FMetasoundFrontendClassVertex& RequiredOutput : RequiredOutputs)
			{
				const FMetasoundFrontendClassOutput* ClassOutput = Graph->FindClassOutputWithName(RequiredOutput.Name).Get();

				bool bIsMatching = false;
				if (nullptr != ClassOutput)
				{
					bIsMatching = FMetasoundFrontendClassVertex::IsFunctionalEquivalent(RequiredOutput, *ClassOutput);
				}

				if (!bIsMatching)
				{
					bDidEdit = true;
					if (nullptr != ClassOutput)
					{
						Graph->RemoveOutputVertex(RequiredOutput.Name);
					}
					Graph->AddOutputVertex(RequiredOutput);
				}
			}

			return bDidEdit;
		}
	}
}

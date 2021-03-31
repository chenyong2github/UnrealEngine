// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFrontendTransform.h"

#include "MetasoundFrontendDocument.h"


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
				if (const FMetasoundFrontendClassInput* ClassInput = Graph->FindClassInputWithName(RequiredInput.Name).Get())
				{
					if (!FMetasoundFrontendClassVertex::IsFunctionalEquivalent(RequiredInput, *ClassInput))
					{
						bDidEdit = true;

						// Cache off node locations to push to new node
						FNodeHandle InputNode = Graph->GetInputNodeWithName(ClassInput->Name);
						const TMap<FGuid, FVector2D> Locations = InputNode->GetNodeStyle().Display.Locations;

						Graph->RemoveInputVertex(RequiredInput.Name);
						InputNode = Graph->AddInputVertex(RequiredInput);

						FMetasoundFrontendNodeStyle Style = InputNode->GetNodeStyle();
						Style.Display.Locations = Locations;
						InputNode->SetNodeStyle(Style);
					}
				}
				else
				{
					bDidEdit = true;
					Graph->AddInputVertex(RequiredInput);
				}
			}

			// Go through each output and add or swap if something is missing.
			for (const FMetasoundFrontendClassVertex& RequiredOutput : RequiredOutputs)
			{
				if (const FMetasoundFrontendClassOutput* ClassOutput = Graph->FindClassOutputWithName(RequiredOutput.Name).Get())
				{
					if (!FMetasoundFrontendClassVertex::IsFunctionalEquivalent(RequiredOutput, *ClassOutput))
					{
						bDidEdit = true;

						// Cache off node locations to push to new node
						FNodeHandle OutputNode = Graph->GetOutputNodeWithName(ClassOutput->Name);
						const TMap<FGuid, FVector2D> Locations = OutputNode->GetNodeStyle().Display.Locations;

						Graph->RemoveOutputVertex(RequiredOutput.Name);
						OutputNode = Graph->AddOutputVertex(RequiredOutput);

						FMetasoundFrontendNodeStyle Style = OutputNode->GetNodeStyle();
						Style.Display.Locations = Locations;
						OutputNode->SetNodeStyle(Style);
					}
				}
				else
				{
					bDidEdit = true;
					Graph->AddOutputVertex(RequiredOutput);
				}
			}

			return bDidEdit;
		}
	} // namespace Frontend
} // namespace Metasound

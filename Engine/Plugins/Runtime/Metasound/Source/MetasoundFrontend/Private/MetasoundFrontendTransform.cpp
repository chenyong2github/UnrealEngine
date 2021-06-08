// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFrontendTransform.h"

#include "MetasoundFrontendDocument.h"
#include "MetasoundLog.h"

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

			const TArray<FMetasoundFrontendClassVertex>& RequiredInputs = Archetype.Interface.Inputs;
			const TArray<FMetasoundFrontendClassVertex>& RequiredOutputs = Archetype.Interface.Outputs;

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

		// Versioning Transforms
		class FVersionDocumentTransform : public IDocumentTransform
		{
			protected:
				virtual FMetasoundFrontendVersionNumber GetTargetVersion() const = 0;
				virtual void TransformInternal(FDocumentHandle InDocument) const = 0;

			public:
				bool Transform(FDocumentHandle InDocument) const override
				{
					const FMetasoundFrontendDocumentMetadata& Metadata = InDocument->GetMetadata();
					if (Metadata.Version.Number >= GetTargetVersion())
					{
						return false;
					}

					TransformInternal(InDocument);

					FMetasoundFrontendDocumentMetadata NewMetadata = Metadata;
					NewMetadata.Version.Number = GetTargetVersion();
					InDocument->SetMetadata(NewMetadata);

					UE_LOG(LogMetaSound, Display, TEXT("Versioned MetaSound Document to %s"), *Metadata.Version.Number.ToString());
					return true;
				}
		};

		/** Versions document from 1.0 to 1.1. */
		class FVersionDocument_1_1 : public FVersionDocumentTransform
		{
		public:
			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 1 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				FGraphHandle GraphHandle = InDocument->GetRootGraph();
				TArray<FNodeHandle> FrontendNodes = GraphHandle->GetNodes();

				// If was connected to a hidden node, then remove that node and set the literal value accordingly.
				for (FNodeHandle& NodeHandle : FrontendNodes)
				{
					if (NodeHandle->GetNodeStyle().Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Hidden)
					{
						const FGuid VertexID = GraphHandle->GetVertexIDForInputVertex(NodeHandle->GetNodeName());
						const FMetasoundFrontendLiteral DefaultLiteral = GraphHandle->GetDefaultInput(VertexID);

						TArray<FOutputHandle> OutputHandles = NodeHandle->GetOutputs();
						if (ensure(OutputHandles.Num() == 1))
						{
							FOutputHandle OutputHandle = OutputHandles[0];
							TArray<FInputHandle> Inputs = OutputHandle->GetConnectedInputs();
							OutputHandle->Disconnect();

							for (FInputHandle& Input : Inputs)
							{
								if (const FMetasoundFrontendLiteral* Literal = Input->GetClassDefaultLiteral())
								{
									if (!Literal->IsEquivalent(DefaultLiteral))
									{
										Input->SetLiteral(DefaultLiteral);
									}
								}
								else
								{
									Input->SetLiteral(DefaultLiteral);
								}
							}

							GraphHandle->RemoveNode(*NodeHandle);
						}
					}
				}
			}
		};

		/** Versions document from 1.1 to 1.2. */
		class FVersionDocument_1_2 : public FVersionDocumentTransform
		{
		private:
			const FName Name;
			const FString& Path;

		public:
			FVersionDocument_1_2(const FName InName, const FString& InPath)
				: Name(InName)
				, Path(InPath)
			{
			}

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return FVersionDocument::GetMaxVersion();
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				FGraphHandle GraphHandle = InDocument->GetRootGraph();
				FMetasoundFrontendClassMetadata Metadata = GraphHandle->GetGraphMetadata();

				Metadata.ClassName = FMetasoundFrontendClassName { "GraphAsset", Name, *Path };
				Metadata.DisplayName = FText::FromString(Name.ToString());

				GraphHandle->SetGraphMetadata(Metadata);
			}
		};

		FVersionDocument::FVersionDocument(FName InName, const FString& InPath)
			: Name(InName)
			, Path(InPath)
		{
		}

		bool FVersionDocument::Transform(FDocumentHandle InDocument) const
		{
			if (!ensure(InDocument->IsValid()))
			{
				return false;
			}

			bool bWasUpdated = false;

			// Add additional transforms here after defining them above, example below.
			bWasUpdated |= FVersionDocument_1_1().Transform(InDocument);
			bWasUpdated |= FVersionDocument_1_2(Name, Path).Transform(InDocument);
			return bWasUpdated;
		}
	} // namespace Frontend
} // namespace Metasound

// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFrontendTransform.h"

#include "CoreMinimal.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundLog.h"
#include "Misc/App.h"


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

				// Before literals could be stored on node inputs directly, they were stored
				// by creating hidden input nodes. Update the doc by finding all hidden input
				// nodes, placing the literal value of the input node directly on the
				// downstream node's input. Then delete the hidden input node.
				for (FNodeHandle& NodeHandle : FrontendNodes)
				{
					const bool bIsHiddenNode = NodeHandle->GetNodeStyle().Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Hidden;
					const bool bIsInputNode = EMetasoundFrontendClassType::Input == NodeHandle->GetClassMetadata().Type;
					const bool bIsHiddenInputNode = bIsHiddenNode && bIsInputNode;

					if (bIsHiddenInputNode)
					{
						// Get literal value from input node.
						const FGuid VertexID = GraphHandle->GetVertexIDForInputVertex(NodeHandle->GetNodeName());
						const FMetasoundFrontendLiteral DefaultLiteral = GraphHandle->GetDefaultInput(VertexID);

						// Apply literal value to downstream node's inputs.
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
						}
						GraphHandle->RemoveNode(*NodeHandle);
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
				return { 1, 2 };
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

		/** Versions document from 1.1 to 1.3. */
		class FVersionDocument_1_3 : public FVersionDocumentTransform
		{
		public:
			FVersionDocument_1_3()
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

				Metadata.ClassName = FMetasoundFrontendClassName { FName(), *FGuid::NewGuid().ToString(), FName() };
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
			bWasUpdated |= FVersionDocument_1_3().Transform(InDocument);

			return bWasUpdated;
		}
	} // namespace Frontend
} // namespace Metasound

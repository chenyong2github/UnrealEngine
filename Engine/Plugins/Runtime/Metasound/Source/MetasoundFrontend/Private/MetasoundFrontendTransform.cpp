// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFrontendTransform.h"

#include "Algo/Transform.h"
#include "CoreMinimal.h"
#include "MetasoundArchetype.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundLog.h"
#include "Misc/App.h"

namespace Metasound
{
	namespace Frontend
	{
		FSwapGraphArchetype::FSwapGraphArchetype(const FMetasoundFrontendArchetype& InFromArchetype, const FMetasoundFrontendArchetype& InToArchetype) 
		{
			InputsToAdd = InToArchetype.Interface.Inputs;
			InputsToRemove = InFromArchetype.Interface.Inputs;
			OutputsToAdd = InToArchetype.Interface.Outputs;
			OutputsToRemove = InFromArchetype.Interface.Outputs;

			// Iterate in reverse to allow removal from `InputsToAdd`
			for (int32 AddIndex = InputsToAdd.Num() - 1; AddIndex >= 0; AddIndex--)
			{
				const FMetasoundFrontendClassVertex& VertexToAdd = InputsToAdd[AddIndex];

				int32 RemoveIndex = InputsToRemove.IndexOfByPredicate([&](const FMetasoundFrontendClassVertex& VertexToRemove)
					{
						return FMetasoundFrontendClassVertex::IsFunctionalEquivalent(VertexToAdd, VertexToRemove);
					});

				if (INDEX_NONE != RemoveIndex)
				{
					PairedInputs.Add(FVertexPair{InputsToRemove[RemoveIndex], InputsToAdd[AddIndex]});
					InputsToRemove.RemoveAtSwap(RemoveIndex);
					InputsToAdd.RemoveAtSwap(AddIndex);
				}
			}

			// Iterate in reverse to allow removal from `OutputsToAdd`
			for (int32 AddIndex = OutputsToAdd.Num() - 1; AddIndex >= 0; AddIndex--)
			{
				const FMetasoundFrontendClassVertex& VertexToAdd = OutputsToAdd[AddIndex];

				int32 RemoveIndex = OutputsToRemove.IndexOfByPredicate([&](const FMetasoundFrontendClassVertex& VertexToRemove)
					{
						return FMetasoundFrontendClassVertex::IsFunctionalEquivalent(VertexToAdd, VertexToRemove);
					});

				if (INDEX_NONE != RemoveIndex)
				{
					PairedOutputs.Add(FVertexPair{OutputsToRemove[RemoveIndex], OutputsToAdd[AddIndex]});
					OutputsToRemove.RemoveAtSwap(RemoveIndex);
					OutputsToAdd.RemoveAtSwap(AddIndex);
				}
			}
		}

		bool FSwapGraphArchetype::Transform(FGraphHandle InGraph) const 
		{
			bool bDidEdit = false;

			// Remove unsupported inputs
			for (const FMetasoundFrontendClassVertex& InputToRemove : InputsToRemove)
			{
				if (const FMetasoundFrontendClassInput* ClassInput = InGraph->FindClassInputWithName(InputToRemove.Name).Get())
				{
					if (FMetasoundFrontendClassInput::IsFunctionalEquivalent(*ClassInput, InputToRemove))
					{
						bDidEdit = true;
						InGraph->RemoveInputVertex(InputToRemove.Name);
					}
				}
			}

			// Remove unrequired outputs
			for (const FMetasoundFrontendClassVertex& OutputToRemove : OutputsToRemove)
			{
				if (const FMetasoundFrontendClassOutput* ClassOutput = InGraph->FindClassOutputWithName(OutputToRemove.Name).Get())
				{
					if (FMetasoundFrontendClassOutput::IsFunctionalEquivalent(*ClassOutput, OutputToRemove))
					{
						bDidEdit = true;
						InGraph->RemoveOutputVertex(OutputToRemove.Name);
					}
				}
			}

			// Add missing inputs
			for (const FMetasoundFrontendClassVertex& InputToAdd : InputsToAdd)
			{
				bDidEdit = true;
				InGraph->AddInputVertex(InputToAdd);
			}

			// Add missing outputs
			for (const FMetasoundFrontendClassVertex& OutputToAdd : OutputsToAdd)
			{
				bDidEdit = true;
				FNodeHandle NewOutputNode = InGraph->AddOutputVertex(OutputToAdd);
				FMetasoundFrontendNodeStyle Style = NewOutputNode->GetNodeStyle();
				// TODO: Create generic "FindGoodLocation()" for nodes. 
				// Inputs are on the left,
				// Outputs are on the right,
				// Place near nodes with similar pin types.
				// Requires a "NodeSize" approximation
				Style.Display.Locations.Add(FGuid(), FVector2D{ 0.f, 0.f });
				NewOutputNode->SetNodeStyle(Style);
			}

			// Swap paired inputs.
			for (const FVertexPair& InputPair : PairedInputs)
			{
				bDidEdit = true;

				const FMetasoundFrontendClassVertex& OriginalVertex = InputPair.Get<0>();
				FMetasoundFrontendClassInput NewVertex = InputPair.Get<1>();

				// Cache off node locations and connections to push to new node
				TMap<FGuid, FVector2D> Locations;
				TArray<FInputHandle> ConnectedInputs;
				if (const FMetasoundFrontendClassInput* ClassInput = InGraph->FindClassInputWithName(OriginalVertex.Name).Get())
				{
					if (FMetasoundFrontendVertex::IsFunctionalEquivalent(*ClassInput, OriginalVertex))
					{
						NewVertex.DefaultLiteral = ClassInput->DefaultLiteral;
						FNodeHandle OriginalInputNode = InGraph->GetInputNodeWithName(OriginalVertex.Name);
						Locations = OriginalInputNode->GetNodeStyle().Display.Locations;

						TArray<FOutputHandle> Outputs = OriginalInputNode->GetOutputsWithVertexName(OriginalVertex.Name);
						if (Outputs.Num() == 1)
						{
							ConnectedInputs = Outputs[0]->GetConnectedInputs();
						}
						InGraph->RemoveInputVertex(OriginalVertex.Name);
					}
				}

				FNodeHandle NewInputNode = InGraph->AddInputVertex(NewVertex);
				// Copy prior node locations
				if (Locations.Num() > 0)
				{
					// TODO: copy entire style.
					FMetasoundFrontendNodeStyle Style = NewInputNode->GetNodeStyle();
					Style.Display.Locations = Locations;
					NewInputNode->SetNodeStyle(Style);
				}

				// Copy prior node connections
				TArray<FOutputHandle> OutputHandles= NewInputNode->GetOutputsWithVertexName(NewVertex.Name);
				if (OutputHandles.Num() == 1)
				{
					FOutputHandle OutputHandle = OutputHandles[0];
					for (FInputHandle& ConnectedInput : ConnectedInputs)
					{
						OutputHandle->Connect(*ConnectedInput);
					}
				}
			}

			// Swap paired outputs.
			for (const FVertexPair& OutputPair : PairedOutputs)
			{
				bDidEdit = true;

				const FMetasoundFrontendClassVertex& OriginalVertex = OutputPair.Get<0>();
				const FMetasoundFrontendClassVertex& NewVertex = OutputPair.Get<1>();

				// Cache off node locations to push to new node
				TMap<FGuid, FVector2D> Locations;
				FOutputHandle ConnectedOutput = IOutputController::GetInvalidHandle();

				// Default add output node to origin.
				Locations.Add(FGuid(), FVector2D{0.f, 0.f});
				if (const FMetasoundFrontendClassOutput* ClassOutput = InGraph->FindClassOutputWithName(OriginalVertex.Name).Get())
				{
					if (FMetasoundFrontendVertex::IsFunctionalEquivalent(*ClassOutput, OriginalVertex))
					{
						FNodeHandle OriginalOutputNode = InGraph->GetOutputNodeWithName(OriginalVertex.Name);
						Locations = OriginalOutputNode->GetNodeStyle().Display.Locations;
						TArray<FInputHandle> Inputs = OriginalOutputNode->GetInputsWithVertexName(OriginalVertex.Name);
						if (Inputs.Num() == 1)
						{
							ConnectedOutput = Inputs[0]->GetConnectedOutput();
						}
						InGraph->RemoveOutputVertex(OriginalVertex.Name);
					}
				}

				FNodeHandle NewOutputNode = InGraph->AddOutputVertex(NewVertex);

				if (Locations.Num() > 0)
				{
					FMetasoundFrontendNodeStyle Style = NewOutputNode->GetNodeStyle();
					Style.Display.Locations = Locations;
					NewOutputNode->SetNodeStyle(Style);
				}

				// Copy prior node connections
				TArray<FInputHandle> InputHandles= NewOutputNode->GetInputsWithVertexName(NewVertex.Name);
				if (InputHandles.Num() == 1)
				{
					ConnectedOutput->Connect(*InputHandles[0]);
				}
			}

			return bDidEdit;
		}

		bool FMatchRootGraphToArchetype::Transform(FDocumentHandle InDocument) const
		{
			bool bDidEdit = false;

			if (!InDocument->IsValid())
			{
				return bDidEdit;
			}

			// Find registered target archetype.
			FMetasoundFrontendArchetype TargetArchetype;
			bool bFoundTargetArchetype = IArchetypeRegistry::Get().FindArchetype(GetArchetypeRegistryKey(ArchetypeVersion), TargetArchetype);

			if (!bFoundTargetArchetype)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Target archetype is not registered [ArchetypeVersion:%s]"), *ArchetypeVersion.ToString());
				return false;
			}

			// Get current archetype version on document.
			const FMetasoundFrontendVersion InitialArchetypeVersion = InDocument->GetArchetypeVersion();

			// Attempt to upgrade
			TArray<const IArchetypeRegistryEntry*> UpgradePath;
			GetUpgradePathForDocument(InitialArchetypeVersion, ArchetypeVersion, UpgradePath);

			if (UpgradeDocumentArchetype(UpgradePath, InDocument))
			{
				bDidEdit = true;
			}

			// Force archetype to conform
			if (ConformDocumentToArchetype(TargetArchetype, InDocument))
			{
				bDidEdit = true;
			}

			return bDidEdit;
		}

		void FMatchRootGraphToArchetype::GetUpgradePathForDocument(const FMetasoundFrontendVersion& InCurrentVersion, const FMetasoundFrontendVersion& InTargetVersion, TArray<const IArchetypeRegistryEntry*>& OutUpgradePath) const
		{
			if (InCurrentVersion.Name == InTargetVersion.Name)
			{
				// Get all associated registered archetypes
				TArray<FMetasoundFrontendVersion> RegisteredVersions = ISearchEngine::Get().FindAllRegisteredArchetypesWithName(InTargetVersion.Name);

				// Filter registry entries that exist between current version and target version
				auto FilterRegistryEntries = [&InCurrentVersion, &InTargetVersion](const FMetasoundFrontendVersion& InVersion)
				{
					const bool bIsGreaterThanCurrent = InVersion.Number > InCurrentVersion.Number;
					const bool bIsLessThanOrEqualToTarget = InVersion.Number <= InTargetVersion.Number;

					return bIsGreaterThanCurrent && bIsLessThanOrEqualToTarget;
				};
				RegisteredVersions = RegisteredVersions.FilterByPredicate(FilterRegistryEntries);

				// sort registry entries to create an ordered upgrade path.
				RegisteredVersions.Sort();

				// Get registry entries from registry keys.
				auto GetRegistryEntry = [](const FMetasoundFrontendVersion& InVersion)
				{
					FArchetypeRegistryKey Key = GetArchetypeRegistryKey(InVersion);
					return IArchetypeRegistry::Get().FindArchetypeRegistryEntry(Key);
				};
				Algo::Transform(RegisteredVersions, OutUpgradePath, GetRegistryEntry);
			}
		}

		bool FMatchRootGraphToArchetype::UpgradeDocumentArchetype(const TArray<const IArchetypeRegistryEntry*>& InUpgradePath, FDocumentHandle InDocument) const
		{
			bool bDidEdit = false;
			for (const IArchetypeRegistryEntry* Entry : InUpgradePath)
			{
				if (ensure(nullptr != Entry))
				{
					if (Entry->UpdateRootGraphArchetype(InDocument))
					{
						bDidEdit = true;
						InDocument->SetArchetypeVersion(Entry->GetArchetype().Version);
					}
				}
			}
			return bDidEdit;
		}

		bool FMatchRootGraphToArchetype::ConformDocumentToArchetype(const FMetasoundFrontendArchetype& InTargetArchetype, FDocumentHandle InDocument) const
		{
			bool bDidEdit = false;

			FMetasoundFrontendArchetype CurrentArchetype;
			FArchetypeRegistryKey CurrentRegistryKey = GetArchetypeRegistryKey(InDocument->GetArchetypeVersion());
			const bool bFoundCurrentArchetype = IArchetypeRegistry::Get().FindArchetype(CurrentRegistryKey, CurrentArchetype);

			if (!bFoundCurrentArchetype)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed to find current archetype on document [ArchetypeVersion:%s]"), *InDocument->GetArchetypeVersion().ToString());
			}

			const bool bIsEqualArchetypeVersion = InDocument->GetArchetypeVersion() == InTargetArchetype.Version;
			const bool bRequiredInterfaceExists = IsSubsetOfClass(InTargetArchetype, InDocument->GetRootGraphClass());

			if (!bIsEqualArchetypeVersion)
			{
				InDocument->SetArchetypeVersion(InTargetArchetype.Version);
				bDidEdit = true;
			}

			if (!(bRequiredInterfaceExists && bIsEqualArchetypeVersion))
			{
				FGraphHandle Graph = InDocument->GetRootGraph();
				if (FSwapGraphArchetype(CurrentArchetype, InTargetArchetype).Transform(Graph))
				{
					bDidEdit = true;
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

		/** Versions document from 1.2 to 1.3. */
		class FVersionDocument_1_3 : public FVersionDocumentTransform
		{
		public:
			FVersionDocument_1_3()
			{
			}

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return {1, 3};
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				FGraphHandle GraphHandle = InDocument->GetRootGraph();
				FMetasoundFrontendClassMetadata Metadata = GraphHandle->GetGraphMetadata();

				Metadata.ClassName = FMetasoundFrontendClassName { FName(), *FGuid::NewGuid().ToString(), FName() };
				GraphHandle->SetGraphMetadata(Metadata);
			}
		};

		/** Versions document from 1.3 to 1.4. */
		class FVersionDocument_1_4 : public FVersionDocumentTransform
		{
		public:
			FVersionDocument_1_4()
			{
			}

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return {1, 4};
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				
				check(InDocument->GetMetadata().Version.Number.Major == 1);
				check(InDocument->GetMetadata().Version.Number.Minor == 3);

				FMetasoundFrontendVersion ArchetypeVersion = InDocument->GetArchetypeVersion();

				// Version 1.3 did not have an "ArchetypeVersion" property on the 
				// document, so any document that is being updated should start off
				// with an "Invalid" archetype version.
				if (ensure(!ArchetypeVersion.IsValid()))
				{
					constexpr bool bIncludeDeprecatedArchetypes = true;
					TArray<FMetasoundFrontendArchetype> AllArchetypes = ISearchEngine::Get().FindAllArchetypes(bIncludeDeprecatedArchetypes);

					const FMetasoundFrontendGraphClass& RootGraph = InDocument->GetRootGraphClass();
					const TArray<FMetasoundFrontendClass>& Dependencies = InDocument->GetDependencies();
					const TArray<FMetasoundFrontendGraphClass>& Subgraphs = InDocument->GetSubgraphs();


					if (const FMetasoundFrontendArchetype* Arch = FindMostSimilarArchetypeSupportingEnvironment(RootGraph, Dependencies, Subgraphs, AllArchetypes))
					{
						UE_LOG(LogMetaSound, Display, TEXT("Assigned archetype [ArchetypeVersion:%s] to document [RootGraphClassName:%s]"), *Arch->Version.ToString(), *RootGraph.Metadata.ClassName.ToString());

						InDocument->SetArchetypeVersion(Arch->Version);
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find archetype for document [RootGraphClassName:%s]"), *RootGraph.Metadata.ClassName.ToString());
					}
				}
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
			bWasUpdated |= FVersionDocument_1_4().Transform(InDocument);

			return bWasUpdated;
		}
	} // namespace Frontend
} // namespace Metasound

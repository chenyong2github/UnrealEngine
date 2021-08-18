// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFrontendTransform.h"

#include "Algo/Transform.h"
#include "CoreMinimal.h"
#include "MetasoundArchetype.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"
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

			auto InputDataTypeCompareFilter = [](FConstNodeHandle NodeHandle, FName DataType)
			{
				bool bMatchesDataType = false;
				NodeHandle->IterateConstOutputs([&](FConstOutputHandle OutputHandle)
				{
					if (OutputHandle->GetDataType() == DataType)
					{
						bMatchesDataType = true;
					}
				});

				return bMatchesDataType;
			};

			auto OutputDataTypeCompareFilter = [](FConstNodeHandle NodeHandle, FName DataType)
			{
				bool bMatchesDataType = false;
				NodeHandle->IterateConstInputs([&](FConstInputHandle InputHandle)
				{
					if (InputHandle->GetDataType() == DataType)
					{
						bMatchesDataType = true;
					}
				});

				return bMatchesDataType;
			};

			auto FindLowestNodeLocationOfClassType = [](EMetasoundFrontendClassType ClassType, FGraphHandle Graph, FName DataType, TFunctionRef<bool(FConstNodeHandle, FName)> NodeDataTypeFilter)
			{
				FVector2D LowestLocation;
				Graph->IterateConstNodes([&](FConstNodeHandle NodeHandle)
				{
					for (const TPair<FGuid, FVector2D>& Pair : NodeHandle->GetNodeStyle().Display.Locations)
					{
						if (Pair.Value.Y > LowestLocation.Y)
						{
							if (NodeDataTypeFilter(NodeHandle, DataType))
							{
								LowestLocation = Pair.Value;
							}
						}
					}
				}, ClassType);

				return LowestLocation;
			};

			// Add missing inputs
			for (const FMetasoundFrontendClassVertex& InputToAdd : InputsToAdd)
			{
				bDidEdit = true;
				FNodeHandle NewInputNode = InGraph->AddInputVertex(InputToAdd);

				FMetasoundFrontendNodeStyle Style = NewInputNode->GetNodeStyle();
				const FVector2D LastOutputLocation = FindLowestNodeLocationOfClassType(EMetasoundFrontendClassType::Input, InGraph, InputToAdd.TypeName, InputDataTypeCompareFilter);
				Style.Display.Locations.Add(FGuid(), LastOutputLocation + DisplayStyle::NodeLayout::DefaultOffsetY);
				NewInputNode->SetNodeStyle(Style);
			}

			// Add missing outputs
			for (const FMetasoundFrontendClassVertex& OutputToAdd : OutputsToAdd)
			{
				bDidEdit = true;
				FNodeHandle NewOutputNode = InGraph->AddOutputVertex(OutputToAdd);

				FMetasoundFrontendNodeStyle Style = NewOutputNode->GetNodeStyle();
				const FVector2D LastOutputLocation = FindLowestNodeLocationOfClassType(EMetasoundFrontendClassType::Output, InGraph, OutputToAdd.TypeName, OutputDataTypeCompareFilter);
				Style.Display.Locations.Add(FGuid(), LastOutputLocation + DisplayStyle::NodeLayout::DefaultOffsetY);
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

		bool FAutoUpdateRootGraph::Transform(FDocumentHandle InDocument) const
		{
			bool bDidEdit = false;

			FMetasoundAssetBase* PresetReferencedMetaSoundAsset = nullptr;
			TArray<TPair<FNodeHandle, FMetasoundFrontendVersionNumber>> NodesToUpdate;

			FGraphHandle RootGraph = InDocument->GetRootGraph();
			RootGraph->IterateNodes([&](FNodeHandle NodeHandle)
			{
				using namespace Metasound::Frontend;
				FClassInterfaceUpdates InterfaceUpdates;
				if (!NodeHandle->CanAutoUpdate(&InterfaceUpdates))
				{
					return;
				}

				const FMetasoundFrontendClassMetadata ClassMetadata = NodeHandle->GetClassMetadata();
				FMetasoundFrontendVersionNumber UpdateVersion = NodeHandle->FindHighestMinorVersionInRegistry();
				if (UpdateVersion.IsValid() && UpdateVersion > ClassMetadata.GetVersion())
				{
					UE_LOG(LogMetaSound, Display, TEXT("Auto-Updating node class '%s': Newer minor version '%s' found."), *ClassMetadata.GetDisplayName().ToString(), *UpdateVersion.ToString());
				}
				else if (InterfaceUpdates.ContainsChanges())
				{
					UpdateVersion = ClassMetadata.GetVersion();
					UE_LOG(LogMetaSound, Display, TEXT("Auto-Updating node with class '%s (%s)': Interface change detected."), *ClassMetadata.GetDisplayName().ToString(), *UpdateVersion.ToString());
				}

				const bool bInterfaceManaged = RootGraph->GetGraphMetadata().GetAutoUpdateManagesInterface();
				if (bInterfaceManaged)
				{
					if (ensure(ClassMetadata.GetType() == EMetasoundFrontendClassType::External))
					{
						const FNodeRegistryKey RegistryKey = FMetasoundFrontendRegistryContainer::Get()->GetRegistryKey(ClassMetadata);
						PresetReferencedMetaSoundAsset = IMetaSoundAssetManager::GetChecked().FindAssetFromKey(RegistryKey);
						if (PresetReferencedMetaSoundAsset)
						{
							return;
						}
					}
				}

				NodesToUpdate.Add(TPair<FNodeHandle, FMetasoundFrontendVersionNumber>(NodeHandle, UpdateVersion));
			}, EMetasoundFrontendClassType::External);

			if (PresetReferencedMetaSoundAsset)
			{
				bDidEdit |= FRebuildPresetRootGraph(PresetReferencedMetaSoundAsset->GetDocumentHandle()).Transform(InDocument);
				bDidEdit |= PresetReferencedMetaSoundAsset->ConformObjectDataToArchetype();
			}
			else
			{
				for (const TPair<FNodeHandle, FMetasoundFrontendVersionNumber>& Pair : NodesToUpdate)
				{
					FNodeHandle ExistingNode = Pair.Key;
					FMetasoundFrontendVersionNumber InitialVersion = ExistingNode->GetClassMetadata().GetVersion();
					FNodeHandle NewNode = ExistingNode->ReplaceWithVersion(Pair.Value);
					FMetasoundFrontendNodeStyle Style = NewNode->GetNodeStyle();
					Style.bMessageNodeUpdated = NewNode->GetClassMetadata().GetVersion() > InitialVersion;
					NewNode->SetNodeStyle(Style);

					bDidEdit |= NewNode->GetID() != ExistingNode->GetID();
				}
			}

			InDocument->SynchronizeDependencies();
			return bDidEdit;
		}

		bool FRebuildPresetRootGraph::Transform(FDocumentHandle InDocument) const
		{
			FGraphHandle RootGraphHandle = InDocument->GetRootGraph();
			if (!ensure(RootGraphHandle->IsValid()))
			{
				return false;
			}

			if (!RootGraphHandle->GetGraphMetadata().GetAutoUpdateManagesInterface())
			{
				return false;
			}

			FConstGraphHandle ReferencedGraphHandle = ReferencedDocument->GetRootGraph();
			if (!ensure(ReferencedGraphHandle->IsValid()))
			{
				return false;
			}

			// 2. Run transform to ensure preset matches reference archetype
			const FMetasoundFrontendVersion& RefArchetypeVersion = ReferencedDocument->GetArchetypeVersion();
			FMatchRootGraphToArchetype(RefArchetypeVersion).Transform(InDocument);

			using FDefaultKey = TPair<FString, FName>;
			struct FDefaultInputValue
			{
				FGuid VertexID;
				FString VertexName;
				FMetasoundFrontendLiteral Literal;
			};
			struct FDefaultOutputValue
			{
				FGuid VertexID;
				FString VertexName;
			};
			TMap<FDefaultKey, FDefaultInputValue> InitInputDefaults;
			TMap<FDefaultKey, FDefaultOutputValue> InitOutputDefaults;

			// 1. Prep by caching existing output guids, input literal values, clearing graph & ensuring ref archetype is supported
			RootGraphHandle->IterateConstNodes([&](FConstNodeHandle InputNode)
			{
				FGuid InputID = InputNode->GetID();
				if (ensure(InputNode->GetNumInputs() == 1))
				{
					InputNode->IterateConstInputs([&](FConstInputHandle Input)
					{
						const FGuid InputVertexID = RootGraphHandle->GetVertexIDForInputVertex(InputNode->GetNodeName());
						FMetasoundFrontendLiteral Literal = RootGraphHandle->GetDefaultInput(InputVertexID);
						FDefaultKey Key(InputNode->GetDisplayName().ToString(), Input->GetDataType());
						FDefaultInputValue Value = { MoveTemp(InputID), InputNode->GetNodeName(), MoveTemp(Literal), };
						InitInputDefaults.Emplace(MoveTemp(Key), MoveTemp(Value));
					});
				}
			}, EMetasoundFrontendClassType::Input);

			RootGraphHandle->IterateConstNodes([&](FConstNodeHandle OutputNode)
			{
				FGuid OutputID = OutputNode->GetID();
				if (ensure(OutputNode->GetNumOutputs() == 1))
				{
					OutputNode->IterateConstOutputs([&](FConstOutputHandle Output)
					{
						FDefaultKey Key(OutputNode->GetDisplayName().ToString(), Output->GetDataType());
						FDefaultOutputValue Value = { MoveTemp(OutputID), OutputNode->GetNodeName() };
						InitOutputDefaults.Emplace(MoveTemp(Key), MoveTemp(Value));
					});
				}
			}, EMetasoundFrontendClassType::Output);

			RootGraphHandle->ClearGraph();

			// 2. Set initial locations of 3 columns (inputs, reference node, and outputs)
			const TArray<FConstNodeHandle> ReferencedInputs = ReferencedGraphHandle->GetConstInputNodes();
			const TArray<FConstNodeHandle> ReferencedOutputs = ReferencedGraphHandle->GetConstOutputNodes();

			FVector2D InputNodeLocation = FVector2D::ZeroVector;

			const float ReferenceNodeYOffset = FMath::Max(ReferencedInputs.Num(), ReferencedOutputs.Num()) * 0.5f;
			const FVector2D ReferenceNodeLocation = InputNodeLocation + FVector2D(DisplayStyle::NodeLayout::DefaultOffsetX.X, ReferenceNodeYOffset);
			FVector2D OutputNodeLocation = InputNodeLocation + (2 * DisplayStyle::NodeLayout::DefaultOffsetX);

			TMap<FString, FString> PresetInputToReferenceInputMap;
			TMap<FString, FString> PresetOutputToReferenceOutputMap;

			// 2. Generate preset's inputs & outputs
			for (const FConstNodeHandle& RefGraphInputNode : ReferencedInputs)
			{
				const FText& DisplayName = RefGraphInputNode->GetDisplayName();
				const FText& Description = RefGraphInputNode->GetDescription();

				// Should only ever be one
				ensure(RefGraphInputNode->GetNumInputs() == 1);
				RefGraphInputNode->IterateConstInputs([&](FConstInputHandle RefGraphInputHandle)
				{
					using namespace Metasound::Frontend;
					FNodeHandle NodeHandle = INodeController::GetInvalidHandle();
					const FName DataTypeName = RefGraphInputHandle->GetDataType();
					const FDefaultKey Key(DisplayName.ToString(), DataTypeName);
					if (const FDefaultInputValue* InputValue = InitInputDefaults.Find(Key))
					{
						FMetasoundFrontendClassInput ClassInput;
						ClassInput.Name = InputValue->VertexName;
						ClassInput.TypeName = DataTypeName;
						ClassInput.Metadata.Description = Description;
						ClassInput.VertexID = InputValue->VertexID;

						NodeHandle = RootGraphHandle->AddInputNode(ClassInput, &InputValue->Literal, &DisplayName);
					}
					else
					{
						FGuid InputID = ReferencedGraphHandle->GetVertexIDForInputVertex(RefGraphInputHandle->GetName());
						const FMetasoundFrontendLiteral DefaultLiteral = ReferencedGraphHandle->GetDefaultInput(InputID);
						NodeHandle = RootGraphHandle->AddInputNode(DataTypeName, Description, &DefaultLiteral, &DisplayName);
					}

					if (ensure(NodeHandle->IsValid()))
					{
						FMetasoundFrontendNodeStyle NodeStyle;
						NodeStyle.Display.Locations.Add(FGuid().NewGuid(), InputNodeLocation);
						NodeHandle->SetNodeStyle(NodeStyle);

						PresetInputToReferenceInputMap.Add(NodeHandle->GetNodeName(), RefGraphInputHandle->GetName());
						InputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
					}
				});
			}

			for (const FConstNodeHandle& RefGraphOutputNode : ReferencedOutputs)
			{
				const FText& DisplayName = RefGraphOutputNode->GetDisplayName();
				const FText& Description = RefGraphOutputNode->GetDescription();

				// Should only ever be one
				ensure(RefGraphOutputNode->GetNumOutputs() == 1);
				RefGraphOutputNode->IterateConstOutputs([&](FConstOutputHandle RefGraphOutputHandle)
				{
					using namespace Metasound::Frontend;

					FNodeHandle NodeHandle = INodeController::GetInvalidHandle();
					const FName DataTypeName = RefGraphOutputHandle->GetDataType();
					const FDefaultKey Key(DisplayName.ToString(), DataTypeName);
					if (FDefaultOutputValue* OutputValue = InitOutputDefaults.Find(Key))
					{
						FMetasoundFrontendClassOutput ClassOutput;
						ClassOutput.Name = OutputValue->VertexName;
						ClassOutput.TypeName = DataTypeName;
						ClassOutput.Metadata.Description = Description;
						ClassOutput.VertexID = OutputValue->VertexID;

						NodeHandle = RootGraphHandle->AddOutputNode(ClassOutput, &DisplayName);
					}
					else
					{
						NodeHandle = RootGraphHandle->AddOutputNode(DataTypeName, Description, &DisplayName);
					}

					if (ensure(NodeHandle->IsValid()))
					{
						FMetasoundFrontendNodeStyle NodeStyle;
						NodeStyle.Display.Locations.Add(FGuid().NewGuid(), OutputNodeLocation);
						NodeHandle->SetNodeStyle(NodeStyle);

						PresetOutputToReferenceOutputMap.Add(NodeHandle->GetNodeName(), RefGraphOutputHandle->GetName());
						OutputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
					}
				});
			}

			// 3. Generate a referencing node to the given referenced MetaSound.
			FMetasoundFrontendClassMetadata ReferencedClassMetadata = ReferencedGraphHandle->GetOwningDocument()->GetRootGraphClass().Metadata;

			// Swap type on look-up as it will be referenced as an externally defined class relative to the new Preset asset
			ReferencedClassMetadata.SetType(EMetasoundFrontendClassType::External);

			FNodeHandle ReferencedNodeHandle = RootGraphHandle->AddNode(ReferencedClassMetadata);
			ensure (ReferencedNodeHandle->IsValid());
			FMetasoundFrontendNodeStyle RefNodeStyle;
			RefNodeStyle.Display.Locations.Add(FGuid::NewGuid(), ReferenceNodeLocation);
			ReferencedNodeHandle->SetNodeStyle(RefNodeStyle);

			// 4. Connect preset's respective inputs & outputs to generated referencing node.
			RootGraphHandle->IterateNodes([&](FNodeHandle InputNode)
			{
				const FString& Name = InputNode->GetNodeName();

				// Should only ever be one
				ensure(InputNode->GetNumOutputs() == 1);
				InputNode->IterateOutputs([&](FOutputHandle OutputHandle)
				{
					const FString& InputName = PresetInputToReferenceInputMap.FindChecked(Name);
					const TArray<FInputHandle> ReferenceInputs = ReferencedNodeHandle->GetInputsWithVertexName(InputName);
					if (ensure(ReferenceInputs.Num() == 1))
					{
						ensure(ReferenceInputs[0]->Connect(*OutputHandle));
					}
				});
			},
			EMetasoundFrontendClassType::Input);

			RootGraphHandle->IterateNodes([&](FNodeHandle OutputNode)
			{
				const FString& Name = OutputNode->GetNodeName();

				// Should only ever be one
				ensure(OutputNode->GetNumInputs() == 1);
				OutputNode->IterateInputs([&](FInputHandle InputHandle)
				{
					const FString& OutputName = PresetOutputToReferenceOutputMap.FindChecked(Name);
					const TArray<FOutputHandle> ReferenceInputs = ReferencedNodeHandle->GetOutputsWithVertexName(OutputName);
					if (ensure(ReferenceInputs.Num() == 1))
					{
						ensure(InputHandle->Connect(*ReferenceInputs[0]));
					}
				});
			},
			EMetasoundFrontendClassType::Output);

			return true;
		}

		bool FSynchronizeAssetClassDisplayName::Transform(FDocumentHandle InDocument) const
		{
			const FMetasoundFrontendClassMetadata& Metadata = InDocument->GetRootGraphClass().Metadata;
			const FText NewAssetName = FText::FromString(AssetName.ToString());

			if (Metadata.GetDisplayName().CompareTo(NewAssetName) != 0)
			{
				FMetasoundFrontendClassMetadata NewMetadata = Metadata;
				NewMetadata.SetDisplayName(NewAssetName);
				InDocument->GetRootGraph()->SetGraphMetadata(NewMetadata);
				return true;
			}

			return false;
		}

		bool FRegenerateAssetClassName::Transform(FDocumentHandle InDocument) const
		{
			FMetasoundFrontendClassMetadata Metadata = InDocument->GetRootGraph()->GetGraphMetadata();
			FMetasoundFrontendClassName NewName = Metadata.GetClassName();
			NewName.Name = *FGuid::NewGuid().ToString();
			Metadata.SetClassName(NewName);
			InDocument->GetRootGraph()->SetGraphMetadata(Metadata);
			return true;
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
					const bool bIsInputNode = EMetasoundFrontendClassType::Input == NodeHandle->GetClassMetadata().GetType();
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
				const FMetasoundFrontendGraphClass& GraphClass = InDocument->GetRootGraphClass();
				FMetasoundFrontendClassMetadata Metadata = GraphClass.Metadata;

				Metadata.SetClassName({ "GraphAsset", Name, *Path });
				Metadata.SetDisplayName(FText::FromString(Name.ToString()));
				InDocument->GetRootGraph()->SetGraphMetadata(Metadata);
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
				const FMetasoundFrontendGraphClass& GraphClass = InDocument->GetRootGraphClass();
				FMetasoundFrontendClassMetadata Metadata = GraphClass.Metadata;

				Metadata.SetClassName(FMetasoundFrontendClassName { FName(), *FGuid::NewGuid().ToString(), FName() });
				InDocument->GetRootGraph()->SetGraphMetadata(Metadata);
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
						UE_LOG(LogMetaSound, Display, TEXT("Assigned archetype [ArchetypeVersion:%s] to document [RootGraphClassName:%s]"),
							*Arch->Version.ToString(), *RootGraph.Metadata.GetClassName().ToString());

						InDocument->SetArchetypeVersion(Arch->Version);
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find archetype for document [RootGraphClassName:%s]"),
							*RootGraph.Metadata.GetClassName().ToString());
					}
				}
			}
		};

		/** Versions document from 1.4 to 1.5. */
		class FVersionDocument_1_5 : public FVersionDocumentTransform
		{
		public:
			FVersionDocument_1_5(FName InAssetName)
				: AssetName(InAssetName)
			{
			}

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 5 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				FSynchronizeAssetClassDisplayName(AssetName).Transform(InDocument);
			}

		private:
			FName AssetName;
		};

		/** Versions document from 1.5 to 1.6. */
		class FVersionDocument_1_6 : public FVersionDocumentTransform
		{
		public:
			FVersionDocument_1_6()
			{
			}

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 6 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				FRegenerateAssetClassName().Transform(InDocument);
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

			const FMetasoundFrontendVersionNumber InitVersionNumber = InDocument->GetMetadata().Version.Number;

			// Add additional transforms here after defining them above, example below.
			bWasUpdated |= FVersionDocument_1_1().Transform(InDocument);
			bWasUpdated |= FVersionDocument_1_2(Name, Path).Transform(InDocument);
			bWasUpdated |= FVersionDocument_1_3().Transform(InDocument);
			bWasUpdated |= FVersionDocument_1_4().Transform(InDocument);
			bWasUpdated |= FVersionDocument_1_5(Name).Transform(InDocument);
			bWasUpdated |= FVersionDocument_1_6().Transform(InDocument);

			if (bWasUpdated)
			{
				const FText& DisplayName = InDocument->GetRootGraph()->GetGraphMetadata().GetDisplayName();
				const FMetasoundFrontendVersionNumber NewVersionNumber = InDocument->GetMetadata().Version.Number;
				UE_LOG(LogMetaSound, Display, TEXT("MetaSound Graph '%s' Parent Document Versioned: '%s' --> '%s'"), *DisplayName.ToString(), *InitVersionNumber.ToString(), *NewVersionNumber.ToString());
			}

			return bWasUpdated;
		}
	} // namespace Frontend
} // namespace Metasound

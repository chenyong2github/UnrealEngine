// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFrontendTransform.h"

#include "Algo/Transform.h"
#include "MetasoundArchetype.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundLog.h"
#include "MetasoundTrace.h"
#include "Misc/App.h"

namespace Metasound
{
	namespace Frontend
	{
		FModifyRootGraphInterfaces::FModifyRootGraphInterfaces(const TArray<FMetasoundFrontendInterface>& InInterfacesToRemove, const TArray<FMetasoundFrontendInterface>& InInterfacesToAdd)
			: InterfacesToRemove(InInterfacesToRemove)
			, InterfacesToAdd(InInterfacesToAdd)
		{
			Init();
		}

		FModifyRootGraphInterfaces::FModifyRootGraphInterfaces(const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToRemove, const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToAdd)
		{
			Algo::Transform(InInterfaceVersionsToRemove, InterfacesToRemove, [](const FMetasoundFrontendVersion& Version)
			{
				FMetasoundFrontendInterface Interface;
				const bool bFromInterfaceFound = IInterfaceRegistry::Get().FindInterface(GetInterfaceRegistryKey(Version), Interface);
				if (!ensureAlways(bFromInterfaceFound))
				{
					UE_LOG(LogMetaSound, Error, TEXT("Failed to find interface '%s' to remove"), *Version.ToString());
				}
				return Interface;
			});

			Algo::Transform(InInterfaceVersionsToAdd, InterfacesToAdd, [](const FMetasoundFrontendVersion& Version)
			{
				FMetasoundFrontendInterface Interface;
				const bool bToInterfaceFound = IInterfaceRegistry::Get().FindInterface(GetInterfaceRegistryKey(Version), Interface);
				if (!ensureAlways(bToInterfaceFound))
				{
					UE_LOG(LogMetaSound, Error, TEXT("Failed to find interface '%s' to add"), *Version.ToString());
				}
				return Interface;
			});

			Init();
		}

		void FModifyRootGraphInterfaces::SetDefaultNodeLocations(bool bInSetDefaultNodeLocations)
		{
			bSetDefaultNodeLocations = bInSetDefaultNodeLocations;
		}

		void FModifyRootGraphInterfaces::SetNamePairingFunction(const TFunction<bool(FName, FName)>& InNamePairingFunction)
		{
			// Reinit required to rebuild list of pairs
			Init(&InNamePairingFunction);
		}

		void FModifyRootGraphInterfaces::Init(const TFunction<bool(FName, FName)>* InNamePairingFunction)
		{
			InputsToRemove.Reset();
			InputsToAdd.Reset();
			OutputsToRemove.Reset();
			OutputsToAdd.Reset();
			PairedInputs.Reset();
			PairedOutputs.Reset();

			for (const FMetasoundFrontendInterface& FromInterface : InterfacesToRemove)
			{
				InputsToRemove.Append(FromInterface.Inputs);
				OutputsToRemove.Append(FromInterface.Outputs);
			}
			for (const FMetasoundFrontendInterface& ToInterface : InterfacesToAdd)
			{
				InputsToAdd.Append(ToInterface.Inputs);
				OutputsToAdd.Append(ToInterface.Outputs);
			}

			// Iterate in reverse to allow removal from `InputsToAdd`
			for (int32 AddIndex = InputsToAdd.Num() - 1; AddIndex >= 0; AddIndex--)
			{
				const FMetasoundFrontendClassVertex& VertexToAdd = InputsToAdd[AddIndex];

				const int32 RemoveIndex = InputsToRemove.IndexOfByPredicate([&](const FMetasoundFrontendClassVertex& VertexToRemove)
				{
					if (VertexToAdd.TypeName != VertexToRemove.TypeName)
					{
						return false;
					}

					if (InNamePairingFunction && *InNamePairingFunction)
					{
						return (*InNamePairingFunction)(VertexToAdd.Name, VertexToRemove.Name);
					}

					FName ParamA;
					FName ParamB;
					FName Namespace;
					VertexToAdd.SplitName(Namespace, ParamA);
					VertexToRemove.SplitName(Namespace, ParamB);

					return ParamA == ParamB;
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

				const int32 RemoveIndex = OutputsToRemove.IndexOfByPredicate([&](const FMetasoundFrontendClassVertex& VertexToRemove)
				{
					if (VertexToAdd.TypeName != VertexToRemove.TypeName)
					{
						return false;
					}

					if (InNamePairingFunction && *InNamePairingFunction)
					{
						return (*InNamePairingFunction)(VertexToAdd.Name, VertexToRemove.Name);
					}

					FName ParamA;
					FName ParamB;
					FName Namespace;
					VertexToAdd.SplitName(Namespace, ParamA);
					VertexToRemove.SplitName(Namespace, ParamB);

					return ParamA == ParamB;
				});

				if (INDEX_NONE != RemoveIndex)
				{
					PairedOutputs.Add(FVertexPair{OutputsToRemove[RemoveIndex], OutputsToAdd[AddIndex]});
					OutputsToRemove.RemoveAtSwap(RemoveIndex);
					OutputsToAdd.RemoveAtSwap(AddIndex);
				}
			}
		}

		bool FModifyRootGraphInterfaces::Transform(FDocumentHandle InDocument) const
		{
			bool bDidEdit = false;

			FGraphHandle GraphHandle = InDocument->GetRootGraph();
			if (!ensure(GraphHandle->IsValid()))
			{
				return false;
			}

			for (const FMetasoundFrontendInterface& Interface : InterfacesToRemove)
			{
				InDocument->RemoveInterfaceVersion(Interface.Version);
			}

			for (const FMetasoundFrontendInterface& Interface : InterfacesToAdd)
			{
				InDocument->AddInterfaceVersion(Interface.Version);
			}

			// Remove unsupported inputs
			for (const FMetasoundFrontendClassVertex& InputToRemove : InputsToRemove)
			{
				if (const FMetasoundFrontendClassInput* ClassInput = GraphHandle->FindClassInputWithName(InputToRemove.Name).Get())
				{
					if (FMetasoundFrontendClassInput::IsFunctionalEquivalent(*ClassInput, InputToRemove))
					{
						bDidEdit = true;
						GraphHandle->RemoveInputVertex(InputToRemove.Name);
					}
				}
			}

			// Remove unrequired outputs
			for (const FMetasoundFrontendClassVertex& OutputToRemove : OutputsToRemove)
			{
				if (const FMetasoundFrontendClassOutput* ClassOutput = GraphHandle->FindClassOutputWithName(OutputToRemove.Name).Get())
				{
					if (FMetasoundFrontendClassOutput::IsFunctionalEquivalent(*ClassOutput, OutputToRemove))
					{
						bDidEdit = true;
						GraphHandle->RemoveOutputVertex(OutputToRemove.Name);
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
			for (const FMetasoundFrontendClassInput& InputToAdd : InputsToAdd)
			{
				bDidEdit = true;
				FNodeHandle NewInputNode = GraphHandle->AddInputVertex(InputToAdd);

				if (bSetDefaultNodeLocations)
				{
					FMetasoundFrontendNodeStyle Style = NewInputNode->GetNodeStyle();
					const FVector2D LastOutputLocation = FindLowestNodeLocationOfClassType(EMetasoundFrontendClassType::Input, GraphHandle, InputToAdd.TypeName, InputDataTypeCompareFilter);
					Style.Display.Locations.Add(FGuid(), LastOutputLocation + DisplayStyle::NodeLayout::DefaultOffsetY);
					NewInputNode->SetNodeStyle(Style);
				}
			}

			// Add missing outputs
			for (const FMetasoundFrontendClassOutput& OutputToAdd : OutputsToAdd)
			{
				bDidEdit = true;
				FNodeHandle NewOutputNode = GraphHandle->AddOutputVertex(OutputToAdd);

				if (bSetDefaultNodeLocations)
				{
					FMetasoundFrontendNodeStyle Style = NewOutputNode->GetNodeStyle();
					const FVector2D LastOutputLocation = FindLowestNodeLocationOfClassType(EMetasoundFrontendClassType::Output, GraphHandle, OutputToAdd.TypeName, OutputDataTypeCompareFilter);
					Style.Display.Locations.Add(FGuid(), LastOutputLocation + DisplayStyle::NodeLayout::DefaultOffsetY);
					NewOutputNode->SetNodeStyle(Style);
				}
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
				if (const FMetasoundFrontendClassInput* ClassInput = GraphHandle->FindClassInputWithName(OriginalVertex.Name).Get())
				{
					if (FMetasoundFrontendVertex::IsFunctionalEquivalent(*ClassInput, OriginalVertex))
					{
						NewVertex.DefaultLiteral = ClassInput->DefaultLiteral;
						NewVertex.NodeID = ClassInput->NodeID;
						FNodeHandle OriginalInputNode = GraphHandle->GetInputNodeWithName(OriginalVertex.Name);
						Locations = OriginalInputNode->GetNodeStyle().Display.Locations;

						FOutputHandle OriginalInputNodeOutput = OriginalInputNode->GetOutputWithVertexName(OriginalVertex.Name);
						ConnectedInputs = OriginalInputNodeOutput->GetConnectedInputs();
						GraphHandle->RemoveInputVertex(OriginalVertex.Name);
					}
				}

				FNodeHandle NewInputNode = GraphHandle->AddInputVertex(NewVertex);
				// Copy prior node locations
				if (!Locations.IsEmpty())
				{
					// TODO: copy entire style.
					FMetasoundFrontendNodeStyle Style = NewInputNode->GetNodeStyle();
					Style.Display.Locations = Locations;
					NewInputNode->SetNodeStyle(Style);
				}

				// Copy prior node connections
				FOutputHandle OutputHandle = NewInputNode->GetOutputWithVertexName(NewVertex.Name);
				for (FInputHandle& ConnectedInput : ConnectedInputs)
				{
					OutputHandle->Connect(*ConnectedInput);
				}
			}

			// Swap paired outputs.
			for (const FVertexPair& OutputPair : PairedOutputs)
			{
				bDidEdit = true;

				const FMetasoundFrontendClassVertex& OriginalVertex = OutputPair.Get<0>();
				FMetasoundFrontendClassVertex NewVertex = OutputPair.Get<1>();

				// Cache off node locations to push to new node
				TMap<FGuid, FVector2D> Locations;
				FOutputHandle ConnectedOutput = IOutputController::GetInvalidHandle();

				// Default add output node to origin.
				Locations.Add(FGuid(), FVector2D{0.f, 0.f});
				if (const FMetasoundFrontendClassOutput* ClassOutput = GraphHandle->FindClassOutputWithName(OriginalVertex.Name).Get())
				{
					if (FMetasoundFrontendVertex::IsFunctionalEquivalent(*ClassOutput, OriginalVertex))
					{
						NewVertex.NodeID = ClassOutput->NodeID;

						// Interface members do not serialize text to avoid localization
						// mismatches between assets and interfaces defined in code.
						NewVertex.Metadata.SetSerializeText(false);

						FNodeHandle OriginalOutputNode = GraphHandle->GetOutputNodeWithName(OriginalVertex.Name);
						Locations = OriginalOutputNode->GetNodeStyle().Display.Locations;
						FInputHandle Input = OriginalOutputNode->GetInputWithVertexName(OriginalVertex.Name);
						ConnectedOutput = Input->GetConnectedOutput();
						GraphHandle->RemoveOutputVertex(OriginalVertex.Name);
					}
				}

				FNodeHandle NewOutputNode = GraphHandle->AddOutputVertex(NewVertex);

				if (Locations.Num() > 0)
				{
					FMetasoundFrontendNodeStyle Style = NewOutputNode->GetNodeStyle();
					Style.Display.Locations = Locations;
					NewOutputNode->SetNodeStyle(Style);
				}

				// Copy prior node connections
				FInputHandle InputHandle = NewOutputNode->GetInputWithVertexName(NewVertex.Name);
				ConnectedOutput->Connect(*InputHandle);
			}

			return bDidEdit;
		}

		bool FUpdateRootGraphInterface::Transform(FDocumentHandle InDocument) const
		{
			bool bDidEdit = false;

			if (!ensure(InDocument->IsValid()))
			{
				return bDidEdit;
			}

			// Find registered target interface.
			FMetasoundFrontendInterface TargetInterface;
			bool bFoundTargetInterface = ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceVersion.Name, TargetInterface);
			if (!bFoundTargetInterface)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Target interface is not registered [InterfaceVersion:%s]"), *InterfaceVersion.ToString());
				return false;
			}

			if (TargetInterface.Version == InterfaceVersion)
			{
				return false;
			}

			// Attempt to upgrade
			TArray<const IInterfaceRegistryEntry*> UpgradePath;
			GetUpdatePathForDocument(InterfaceVersion, TargetInterface.Version, UpgradePath);
			return UpdateDocumentInterface(UpgradePath, InDocument);
		}

		void FUpdateRootGraphInterface::GetUpdatePathForDocument(const FMetasoundFrontendVersion& InCurrentVersion, const FMetasoundFrontendVersion& InTargetVersion, TArray<const IInterfaceRegistryEntry*>& OutUpgradePath) const
		{
			if (InCurrentVersion.Name == InTargetVersion.Name)
			{
				// Get all associated registered interfaces
				TArray<FMetasoundFrontendVersion> RegisteredVersions = ISearchEngine::Get().FindAllRegisteredInterfacesWithName(InTargetVersion.Name);

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
					FInterfaceRegistryKey Key = GetInterfaceRegistryKey(InVersion);
					return IInterfaceRegistry::Get().FindInterfaceRegistryEntry(Key);
				};
				Algo::Transform(RegisteredVersions, OutUpgradePath, GetRegistryEntry);
			}
		}

		bool FUpdateRootGraphInterface::UpdateDocumentInterface(const TArray<const IInterfaceRegistryEntry*>& InUpgradePath, FDocumentHandle InDocument) const
		{
			const FMetasoundFrontendVersionNumber* LastVersionUpdated = nullptr;
			for (const IInterfaceRegistryEntry* Entry : InUpgradePath)
			{
				if (ensure(nullptr != Entry))
				{
					if (Entry->UpdateRootGraphInterface(InDocument))
					{
						LastVersionUpdated = &Entry->GetInterface().Version.Number;
					}
				}
			}

			if (LastVersionUpdated)
			{
				UE_LOG(LogMetaSound, Display, TEXT("Asset '%s' interface '%s' updated: '%s' --> '%s'"),
					*InDocument->GetRootGraphClass().Metadata.GetDisplayName().ToString(),
					*InterfaceVersion.Name.ToString(),
					*InterfaceVersion.Number.ToString(),
					*LastVersionUpdated->ToString());
				return true;
			}

			return false;
		}

		bool FAutoUpdateRootGraph::Transform(FDocumentHandle InDocument) const
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FAutoUpdateRootGraph::Transform);
			bool bDidEdit = false;

			FMetasoundAssetBase* PresetReferencedMetaSoundAsset = nullptr;
			TArray<TPair<FNodeHandle, FMetasoundFrontendVersionNumber>> NodesToUpdate;

			FGraphHandle RootGraph = InDocument->GetRootGraph();
			const bool bInterfaceManaged = RootGraph->GetGraphMetadata().GetAutoUpdateManagesInterface();
			RootGraph->IterateNodes([&](FNodeHandle NodeHandle)
			{
				using namespace Metasound::Frontend;

				const FMetasoundFrontendClassMetadata& ClassMetadata = NodeHandle->GetClassMetadata();

				if (bInterfaceManaged)
				{
					const FNodeRegistryKey RegistryKey = FMetasoundFrontendRegistryContainer::Get()->GetRegistryKey(ClassMetadata);
					PresetReferencedMetaSoundAsset = IMetaSoundAssetManager::GetChecked().TryLoadAssetFromKey(RegistryKey);
					if (!PresetReferencedMetaSoundAsset)
					{
						UE_LOG(LogMetaSound, Error, TEXT("Auto-Updating preset '%s' failed: Referenced class '%s' missing."), *DebugAssetPath, *ClassMetadata.GetClassName().ToString());
					}
					return;
				}

				FClassInterfaceUpdates InterfaceUpdates;
				if (!NodeHandle->CanAutoUpdate(InterfaceUpdates))
				{
					return;
				}

				FMetasoundFrontendVersionNumber UpdateVersion = NodeHandle->FindHighestMinorVersionInRegistry();
				if (UpdateVersion.IsValid() && UpdateVersion > ClassMetadata.GetVersion())
				{
					UE_LOG(LogMetaSound, Display, TEXT("Auto-Updating '%s' node class '%s': Newer version '%s' found."), *DebugAssetPath, *ClassMetadata.GetClassName().ToString(), *UpdateVersion.ToString());

					NodesToUpdate.Add(TPair<FNodeHandle, FMetasoundFrontendVersionNumber>(NodeHandle, UpdateVersion));
				}
				else if (InterfaceUpdates.ContainsChanges())
				{
					UpdateVersion = ClassMetadata.GetVersion();
					UE_LOG(LogMetaSound, Display, TEXT("Auto-Updating '%s' node class '%s (%s)': Interface change detected."), *DebugAssetPath, *ClassMetadata.GetClassName().ToString(), *UpdateVersion.ToString());

					NodesToUpdate.Add(TPair<FNodeHandle, FMetasoundFrontendVersionNumber>(NodeHandle, UpdateVersion));
				}

				// Only update the node at this point if editor data is loaded. If it isn't and their are no interface
				// changes but auto-update returned it was eligible, then the auto-update only contains cosmetic changes.
#if WITH_EDITORONLY_DATA
				else
				{
					NodesToUpdate.Add(TPair<FNodeHandle, FMetasoundFrontendVersionNumber>(NodeHandle, UpdateVersion));
				}
#endif // WITH_EDITORONLY_DATA
			}, EMetasoundFrontendClassType::External);

			if (PresetReferencedMetaSoundAsset)
			{
				if (bInterfaceManaged)
				{
					bDidEdit |= FRebuildPresetRootGraph(PresetReferencedMetaSoundAsset->GetDocumentHandle()).Transform(InDocument);
					if (bDidEdit)
					{
						FMetasoundFrontendClassMetadata ParentMetadata = InDocument->GetRootGraphClass().Metadata;
						ParentMetadata.SetType(EMetasoundFrontendClassType::External);
						const FNodeRegistryKey RegistryKey = FMetasoundFrontendRegistryContainer::Get()->GetRegistryKey(ParentMetadata);
						FMetasoundAssetBase* ParentMetaSoundAsset = IMetaSoundAssetManager::GetChecked().TryLoadAssetFromKey(RegistryKey);
						if (ensure(ParentMetaSoundAsset))
						{
							ParentMetaSoundAsset->ConformObjectDataToInterfaces();
						}

						InDocument->RemoveUnreferencedDependencies();
						InDocument->SynchronizeDependencyMetadata();
					}
				}
			}
			else
			{
				bDidEdit |= !NodesToUpdate.IsEmpty();
				for (const TPair<FNodeHandle, FMetasoundFrontendVersionNumber>& Pair : NodesToUpdate)
				{
					FNodeHandle ExistingNode = Pair.Key;
					FMetasoundFrontendVersionNumber InitialVersion = ExistingNode->GetClassMetadata().GetVersion();
					FNodeHandle NewNode = ExistingNode->ReplaceWithVersion(Pair.Value);
					FMetasoundFrontendNodeStyle Style = NewNode->GetNodeStyle();
					Style.bMessageNodeUpdated = NewNode->GetClassMetadata().GetVersion() > InitialVersion;
					NewNode->SetNodeStyle(Style);
				}

				InDocument->RemoveUnreferencedDependencies();
				InDocument->SynchronizeDependencyMetadata();
			}

			return bDidEdit;
		}

		bool FRebuildPresetRootGraph::Transform(FDocumentHandle InDocument) const
		{
			FGraphHandle RootGraphHandle = InDocument->GetRootGraph();
			if (!ensure(RootGraphHandle->IsValid()))
			{
				return false;
			}

			// Callers of this transform should check that the graph is supposed to
			// be managed externally before calling this transform. If a scenario
			// arises where this transform is used outside of AutoUpdate, then this
			// early exist should be removed as it's mostly here to protect against
			// accidental manipulation of metasound graphs.
			if (!ensure(RootGraphHandle->GetGraphMetadata().GetAutoUpdateManagesInterface()))
			{
				return false;
			}

			FConstGraphHandle ReferencedGraphHandle = ReferencedDocument->GetRootGraph();
			if (!ensure(ReferencedGraphHandle->IsValid()))
			{
				return false;
			}

			// Determine the inputs and outputs needed in the wrapping graph. Also
			// cache any exiting literals that have been set on the wrapping graph.
			TArray<FMetasoundFrontendClassInput> ClassInputs = GenerateRequiredClassInputs(RootGraphHandle);
			TArray<FMetasoundFrontendClassOutput> ClassOutputs = GenerateRequiredClassOutputs(RootGraphHandle);

			FGuid PresetNodeID;
			RootGraphHandle->IterateConstNodes([InPresetNodeID = &PresetNodeID](FConstNodeHandle PresetNodeHandle)
			{
				*InPresetNodeID = PresetNodeHandle->GetID();
			}, EMetasoundFrontendClassType::External);

			// Clear the root graph so it can be rebuilt.
			RootGraphHandle->ClearGraph();

			// Ensure preset interfaces match those found in referenced graph.  Referenced graph is assumed to be
			// well-formed (i.e. all inputs/outputs/environment variables declared by interfaces are present, and
			// of proper name & data type).
			const TSet<FMetasoundFrontendVersion>& RefInterfaceVersions = ReferencedDocument->GetInterfaceVersions();
			for (const FMetasoundFrontendVersion& Version : RefInterfaceVersions)
			{
				InDocument->AddInterfaceVersion(Version);
			}

			// Add referenced node
			FMetasoundFrontendClassMetadata ReferencedClassMetadata = ReferencedGraphHandle->GetGraphMetadata();
			// Swap type on look-up as it will be referenced as an externally defined class relative to the new Preset asset
			ReferencedClassMetadata.SetType(EMetasoundFrontendClassType::External);

			// Set node location.
			FNodeHandle ReferencedNodeHandle = RootGraphHandle->AddNode(ReferencedClassMetadata, PresetNodeID);
			FMetasoundFrontendNodeStyle RefNodeStyle;

			// Offset to be to the right of input nodes
			RefNodeStyle.Display.Locations.Add(FGuid::NewGuid(), DisplayStyle::NodeLayout::DefaultOffsetX);
			ReferencedNodeHandle->SetNodeStyle(RefNodeStyle);

			// Connect parent graph to referenced graph
			AddAndConnectInputs(ClassInputs, RootGraphHandle, ReferencedNodeHandle);
			AddAndConnectOutputs(ClassOutputs, RootGraphHandle, ReferencedNodeHandle);

			return true;
		}

		void FRebuildPresetRootGraph::AddAndConnectInputs(const TArray<FMetasoundFrontendClassInput>& InClassInputs, FGraphHandle& InParentGraphHandle, FNodeHandle& InReferencedNode) const
		{
			// Add inputs and space appropriately
			FVector2D InputNodeLocation = FVector2D::ZeroVector;

			for (const FMetasoundFrontendClassInput& ClassInput : InClassInputs)
			{
				FNodeHandle InputNode = InParentGraphHandle->AddInputVertex(ClassInput);

				if (ensure(InputNode->IsValid()))
				{
					// Set input node location
					FMetasoundFrontendNodeStyle NodeStyle;
					NodeStyle.Display.Locations.Add(FGuid::NewGuid(), InputNodeLocation);
					InputNode->SetNodeStyle(NodeStyle);
					InputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;

					// Connect input node to corresponding referencing node.
					FOutputHandle OutputToConnect = InputNode->GetOutputWithVertexName(ClassInput.Name);
					FInputHandle InputToConnect = InReferencedNode->GetInputWithVertexName(ClassInput.Name);
					bool bSuccess = OutputToConnect->Connect(*InputToConnect);
					check(bSuccess);
				}
			}
		}

		void FRebuildPresetRootGraph::AddAndConnectOutputs(const TArray<FMetasoundFrontendClassOutput>& InClassOutputs, FGraphHandle& InParentGraphHandle, FNodeHandle& InReferencedNode) const
		{
			// Add outputs and space appropriately
			FVector2D OutputNodeLocation = (2 * DisplayStyle::NodeLayout::DefaultOffsetX);

			for (const FMetasoundFrontendClassOutput& ClassOutput : InClassOutputs)
			{
				FNodeHandle OutputNode = InParentGraphHandle->AddOutputVertex(ClassOutput);
				
				if (ensure(OutputNode->IsValid()))
				{
					// Set input node location
					FMetasoundFrontendNodeStyle NodeStyle;
					NodeStyle.Display.Locations.Add(FGuid::NewGuid(), OutputNodeLocation);
					OutputNode->SetNodeStyle(NodeStyle);
					OutputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;

					// Connect input node to corresponding referenced node. 
					FInputHandle InputToConnect = OutputNode->GetInputWithVertexName(ClassOutput.Name);
					FOutputHandle OutputToConnect = InReferencedNode->GetOutputWithVertexName(ClassOutput.Name);
					bool bSuccess = InputToConnect->Connect(*OutputToConnect);
					check(bSuccess);
				}
			}
		}

		TArray<FMetasoundFrontendClassInput> FRebuildPresetRootGraph::GenerateRequiredClassInputs(const FConstGraphHandle& InParentGraph) const
		{
			TArray<FMetasoundFrontendClassInput> ClassInputs;

			FConstGraphHandle ReferencedGraph = ReferencedDocument->GetRootGraph();

			// Iterate through all input nodes of referenced graph
			ReferencedGraph->IterateConstNodes([&](FConstNodeHandle InputNode)
			{
				const FName NodeName = InputNode->GetNodeName();
				FConstInputHandle Input = InputNode->GetConstInputWithVertexName(NodeName);
				if (ensure(Input->IsValid()))
				{
					FMetasoundFrontendClassInput ClassInput;

					ClassInput.Name = NodeName;
					ClassInput.TypeName = Input->GetDataType();

					ClassInput.Metadata.SetDescription(InputNode->GetDescription());
					ClassInput.Metadata.SetDisplayName(Input->GetMetadata().GetDisplayName());

					ClassInput.VertexID = FGuid::NewGuid();

					if (const FMetasoundFrontendClassInput* ExistingClassInput = InParentGraph->FindClassInputWithName(NodeName).Get())
					{
						ClassInput.NodeID = ExistingClassInput->NodeID;
					}

					if (InParentGraph->ContainsInputVertex(NodeName, ClassInput.TypeName))
					{
						// If the input vertex already exists in the parent graph,
						// use the default literal value from the parent graph.
						FGuid VertexID = InParentGraph->GetVertexIDForInputVertex(NodeName);
						ClassInput.DefaultLiteral = InParentGraph->GetDefaultInput(VertexID);
					}
					else
					{
						// If the input vertex does not exist on the parent graph,
						// then it is a new vertex and should use the default value
						// of the referenced graph.
						const FGuid ReferencedVertexID = ReferencedGraph->GetVertexIDForInputVertex(NodeName);
						ClassInput.DefaultLiteral = ReferencedGraph->GetDefaultInput(ReferencedVertexID);
					}

					ClassInputs.Add(MoveTemp(ClassInput));
				}
			}, EMetasoundFrontendClassType::Input);

			return ClassInputs;
		}

		TArray<FMetasoundFrontendClassOutput> FRebuildPresetRootGraph::GenerateRequiredClassOutputs(const FConstGraphHandle& InParentGraph) const
		{
			TArray<FMetasoundFrontendClassOutput> ClassOutputs;

			FConstGraphHandle ReferencedGraph = ReferencedDocument->GetRootGraph();

			// Iterate over the referenced graph's output nodes.
			ReferencedGraph->IterateConstNodes([&](FConstNodeHandle OutputNode)
			{
				const FName NodeName = OutputNode->GetNodeName();
				FConstOutputHandle Output = OutputNode->GetConstOutputWithVertexName(NodeName);
				if (ensure(Output->IsValid()))
				{
					FMetasoundFrontendClassOutput ClassOutput;

					ClassOutput.Name = NodeName;
					ClassOutput.TypeName = Output->GetDataType();

					ClassOutput.Metadata.SetDescription(OutputNode->GetDescription());
					ClassOutput.Metadata.SetDisplayName(Output->GetMetadata().GetDisplayName());

					ClassOutput.VertexID = FGuid::NewGuid();

					if (const FMetasoundFrontendClassOutput* ExistingClassOutput = InParentGraph->FindClassOutputWithName(NodeName).Get())
					{
						ClassOutput.NodeID = ExistingClassOutput->NodeID;
					}

					ClassOutputs.Add(MoveTemp(ClassOutput));
				}
			}, EMetasoundFrontendClassType::Output);

			return ClassOutputs;
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

				const TSet<FMetasoundFrontendVersion>& Interfaces = InDocument->GetInterfaceVersions();

				// Version 1.3 did not have an "InterfaceVersion" property on the
				// document, so any document that is being updated should start off
				// with an "Invalid" interface version.
				if (ensure(Interfaces.IsEmpty()))
				{
					constexpr bool bIncludeAllVersions = true;
					TArray<FMetasoundFrontendInterface> AllInterfaces = ISearchEngine::Get().FindAllInterfaces(bIncludeAllVersions);

					const FMetasoundFrontendGraphClass& RootGraph = InDocument->GetRootGraphClass();
					const TArray<FMetasoundFrontendClass>& Dependencies = InDocument->GetDependencies();
					const TArray<FMetasoundFrontendGraphClass>& Subgraphs = InDocument->GetSubgraphs();

					if (const FMetasoundFrontendInterface* Interface = FindMostSimilarInterfaceSupportingEnvironment(RootGraph, Dependencies, Subgraphs, AllInterfaces))
					{
						UE_LOG(LogMetaSound, Display, TEXT("Assigned interface [InterfaceVersion:%s] to document [RootGraphClassName:%s]"),
							*Interface->Version.ToString(), *RootGraph.Metadata.GetClassName().ToString());

						InDocument->AddInterfaceVersion(Interface->Version);
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find interface for document [RootGraphClassName:%s]"),
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
				const FMetasoundFrontendClassMetadata& Metadata = InDocument->GetRootGraphClass().Metadata;
				const FText NewAssetName = FText::FromString(AssetName.ToString());
				if (Metadata.GetDisplayName().CompareTo(NewAssetName) != 0)
				{
					FMetasoundFrontendClassMetadata NewMetadata = Metadata;
					NewMetadata.SetDisplayName(NewAssetName);
					InDocument->GetRootGraph()->SetGraphMetadata(NewMetadata);
				}
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

		/** Versions document from 1.6 to 1.7. */
		class FVersionDocument_1_7 : public FVersionDocumentTransform
		{
		public:
			FVersionDocument_1_7() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 7 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				auto RenameTransform = [](FNodeHandle NodeHandle)
				{
					// Required nodes are all (at the point of this transform) providing
					// unique names and customized display names (ex. 'Audio' for both mono &
					// L/R output, On Play, & 'On Finished'), so do not replace them by nulling
					// out the guid as a name and using the converted FName of the FText DisplayName.
					if (!NodeHandle->IsInterfaceMember())
					{
						const FName NewNodeName = *NodeHandle->GetDisplayName().ToString();
						NodeHandle->IterateInputs([&](FInputHandle InputHandle)
						{
							InputHandle->SetName(NewNodeName);
						});

						NodeHandle->IterateOutputs([&](FOutputHandle OutputHandle)
						{
							OutputHandle->SetName(NewNodeName);
						});

						NodeHandle->SetDisplayName(FText());
						NodeHandle->SetNodeName(NewNodeName);
					}
				};

				InDocument->GetRootGraph()->IterateNodes(RenameTransform, EMetasoundFrontendClassType::Input);
				InDocument->GetRootGraph()->IterateNodes(RenameTransform, EMetasoundFrontendClassType::Output);
			}
		};

		/** Versions document from 1.7 to 1.8. */
		class FVersionDocument_1_8 : public FVersionDocumentTransform
		{
		public:
			FVersionDocument_1_8() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 8 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				// Do not serialize MetaData text for dependencies as
				// CacheRegistryData dynamically provides this.
				InDocument->IterateDependencies([](FMetasoundFrontendClass& Dependency)
				{
					constexpr bool bSerializeText = false;
					Dependency.Metadata.SetSerializeText(bSerializeText);

					for (FMetasoundFrontendClassInput& Input : Dependency.Interface.Inputs)
					{
						Input.Metadata.SetSerializeText(false);
					}

					for (FMetasoundFrontendClassOutput& Output : Dependency.Interface.Outputs)
					{
						Output.Metadata.SetSerializeText(false);
					}
				});

				const TSet<FMetasoundFrontendVersion>& InterfaceVersions = InDocument->GetInterfaceVersions();

				using FNameDataTypePair = TPair<FName, FName>;
				TSet<FNameDataTypePair> InterfaceInputs;
				TSet<FNameDataTypePair> InterfaceOutputs;

				for (const FMetasoundFrontendVersion& Version : InterfaceVersions)
				{
					FInterfaceRegistryKey RegistryKey = GetInterfaceRegistryKey(Version);
					const IInterfaceRegistryEntry* Entry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(RegistryKey);
					if (ensure(Entry))
					{
						const FMetasoundFrontendInterface& Interface = Entry->GetInterface();
						Algo::Transform(Interface.Inputs, InterfaceInputs, [](const FMetasoundFrontendClassInput& Input)
						{
							return FNameDataTypePair(Input.Name, Input.TypeName);
						});

						Algo::Transform(Interface.Outputs, InterfaceOutputs, [](const FMetasoundFrontendClassOutput& Output)
						{
							return FNameDataTypePair(Output.Name, Output.TypeName);
						});
					}
				}

				// Only serialize MetaData text for inputs owned by the graph (not by interfaces)
				FMetasoundFrontendGraphClass RootGraphClass = InDocument->GetRootGraphClass();
				for (FMetasoundFrontendClassInput& Input : RootGraphClass.Interface.Inputs)
				{
					const bool bSerializeText = !InterfaceInputs.Contains(FNameDataTypePair(Input.Name, Input.TypeName));
					Input.Metadata.SetSerializeText(bSerializeText);
				}

				// Only serialize MetaData text for outputs owned by the graph (not by interfaces)
				for (FMetasoundFrontendClassOutput& Output : RootGraphClass.Interface.Outputs)
				{
					const bool bSerializeText = !InterfaceOutputs.Contains(FNameDataTypePair(Output.Name, Output.TypeName));
					Output.Metadata.SetSerializeText(bSerializeText);
				}

				InDocument->SetRootGraphClass(MoveTemp(RootGraphClass));
			}
		};

		/** Versions document from 1.8 to 1.9. */
		class FVersionDocument_1_9 : public FVersionDocumentTransform
		{
		public:
			FVersionDocument_1_9() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 9 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				// Display name text is no longer copied at this versioning point for assets
				// from the asset's FName to avoid FText warnings regarding generation from
				// an FString.  It also avoids desync if asset gets moved.
				FMetasoundFrontendGraphClass RootGraphClass = InDocument->GetRootGraphClass();
				RootGraphClass.Metadata.SetDisplayName(FText());
				InDocument->SetRootGraphClass(MoveTemp(RootGraphClass));
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
			bWasUpdated |= FVersionDocument_1_7().Transform(InDocument);
			bWasUpdated |= FVersionDocument_1_8().Transform(InDocument);
			bWasUpdated |= FVersionDocument_1_9().Transform(InDocument);

			if (bWasUpdated)
			{
				const FText& DisplayName = InDocument->GetRootGraph()->GetGraphMetadata().GetDisplayName();
				const FMetasoundFrontendVersionNumber NewVersionNumber = InDocument->GetMetadata().Version.Number;
				UE_LOG(LogMetaSound, Display, TEXT("MetaSound at '%s' Document Versioned: '%s' --> '%s'"), *Path, *InitVersionNumber.ToString(), *NewVersionNumber.ToString());
			}

			return bWasUpdated;
		}
	} // namespace Frontend
} // namespace Metasound

// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFrontendDocumentBuilder.h"

#include "Algo/AnyOf.h"
#include "Algo/Find.h"
#include "Algo/ForEach.h"
#include "Algo/NoneOf.h"
#include "Algo/Sort.h"
#include "Algo/Transform.h"
#include "Interfaces/MetasoundFrontendInterfaceBindingRegistry.h"
#include "Interfaces/MetasoundFrontendInterfaceRegistry.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAssetManager.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocumentCache.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundTrace.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetasoundFrontendDocumentBuilder)


namespace Metasound::Frontend
{
	namespace DocumentBuilderPrivate
	{
		bool TryGetInterfaceBoundEdges(
			const FGuid& InFromNodeID,
			const FMetasoundFrontendDocument& InFromNodeDocument,
			const FGuid& InToNodeID,
			const FMetasoundFrontendDocument& InToNodeDocument,
			TSet<FNamedEdge>& OutNamedEdges)
		{
			OutNamedEdges.Reset();
			TSet<FName> InputNames;
			const TSet<FMetasoundFrontendVersion>& OutputInterfaceVersions = InFromNodeDocument.Interfaces;
			for (const FMetasoundFrontendVersion& InputInterfaceVersion : InToNodeDocument.Interfaces)
			{
				TArray<const FInterfaceBindingRegistryEntry*> BindingEntries;
				if (IInterfaceBindingRegistry::Get().FindInterfaceBindingEntries(InputInterfaceVersion, BindingEntries))
				{
					Algo::Sort(BindingEntries, [](const FInterfaceBindingRegistryEntry* A, const FInterfaceBindingRegistryEntry* B)
					{
						check(A);
						check(B);
						return A->GetBindingPriority() < B->GetBindingPriority();
					});

					// Bindings are sorted in registry with earlier entries being higher priority to apply connections,
					// so earlier listed connections are selected over potential collisions with later entries.
					for (const FInterfaceBindingRegistryEntry* BindingEntry : BindingEntries)
					{
						check(BindingEntry);
						if (OutputInterfaceVersions.Contains(BindingEntry->GetOutputInterfaceVersion()))
						{
							for (const FMetasoundFrontendInterfaceVertexBinding& VertexBinding : BindingEntry->GetVertexBindings())
							{
								if (!InputNames.Contains(VertexBinding.InputName))
								{
									InputNames.Add(VertexBinding.InputName);
									OutNamedEdges.Add(FNamedEdge { InFromNodeID, VertexBinding.OutputName, InToNodeID, VertexBinding.InputName });
								}
							}
						}
					}
				}
			};

			return true;
		}

		void FinalizeGraphVertexNode(FMetasoundFrontendNode& InOutNode, const FMetasoundFrontendClassVertex& InVertex)
		{
			InOutNode.Name = InVertex.Name;
			// Set name on related vertices of input node
			auto IsVertexWithTypeName = [&InVertex](const FMetasoundFrontendVertex& Vertex) { return Vertex.TypeName == InVertex.TypeName; };
			if (FMetasoundFrontendVertex* InputVertex = InOutNode.Interface.Inputs.FindByPredicate(IsVertexWithTypeName))
			{
				InputVertex->Name = InVertex.Name;
			}
			else
			{
				UE_LOG(LogMetaSound, Error, TEXT("Node associated with graph vertex of type '%s' does not contain input vertex of matching type."), *InVertex.TypeName.ToString());
			}

			if (FMetasoundFrontendVertex* OutputVertex = InOutNode.Interface.Outputs.FindByPredicate(IsVertexWithTypeName))
			{
				OutputVertex->Name = InVertex.Name;
			}
			else
			{
				UE_LOG(LogMetaSound, Error, TEXT("Node associated with graph vertex of type '%s' does not contain output vertex of matching type."), *InVertex.TypeName.ToString());
			}
		}

		class FModifyInterfacesImpl
		{
		public:
			FModifyInterfacesImpl(FModifyInterfaceOptions&& InOptions)
				: Options(MoveTemp(InOptions))
			{
				for (const FMetasoundFrontendInterface& FromInterface : Options.InterfacesToRemove)
				{
					InputsToRemove.Append(FromInterface.Inputs);
					OutputsToRemove.Append(FromInterface.Outputs);
				}

				for (const FMetasoundFrontendInterface& ToInterface : Options.InterfacesToAdd)
				{
					Algo::Transform(ToInterface.Inputs, InputsToAdd, [&ToInterface](const FMetasoundFrontendClassInput& Input)
					{
						FMetasoundFrontendClassInput NewInput = Input;
						NewInput.NodeID = FGuid::NewGuid();
						NewInput.VertexID = FGuid::NewGuid();
						return FInputInterfacePair { MoveTemp(NewInput), &ToInterface };
					});

					Algo::Transform(ToInterface.Outputs, OutputsToAdd, [&ToInterface](const FMetasoundFrontendClassOutput& Output)
					{
						FMetasoundFrontendClassOutput NewOutput = Output;
						NewOutput.NodeID = FGuid::NewGuid();
						NewOutput.VertexID = FGuid::NewGuid();
						return FOutputInterfacePair { MoveTemp(NewOutput), &ToInterface };
					});
				}

				// Iterate in reverse to allow removal from `InputsToAdd`
				for (int32 AddIndex = InputsToAdd.Num() - 1; AddIndex >= 0; AddIndex--)
				{
					const FMetasoundFrontendClassVertex& VertexToAdd = InputsToAdd[AddIndex].Key;

					const int32 RemoveIndex = InputsToRemove.IndexOfByPredicate([&](const FMetasoundFrontendClassVertex& VertexToRemove)
						{
							if (VertexToAdd.TypeName != VertexToRemove.TypeName)
							{
								return false;
							}

							if (Options.NamePairingFunction)
							{
								return Options.NamePairingFunction(VertexToAdd.Name, VertexToRemove.Name);
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
						PairedInputs.Add(FVertexPair { InputsToRemove[RemoveIndex], InputsToAdd[AddIndex].Key });
						InputsToRemove.RemoveAtSwap(RemoveIndex);
						InputsToAdd.RemoveAtSwap(AddIndex);
					}
				}

				// Iterate in reverse to allow removal from `OutputsToAdd`
				for (int32 AddIndex = OutputsToAdd.Num() - 1; AddIndex >= 0; AddIndex--)
				{
					const FMetasoundFrontendClassVertex& VertexToAdd = OutputsToAdd[AddIndex].Key;

					const int32 RemoveIndex = OutputsToRemove.IndexOfByPredicate([&](const FMetasoundFrontendClassVertex& VertexToRemove)
						{
							if (VertexToAdd.TypeName != VertexToRemove.TypeName)
							{
								return false;
							}

							if (Options.NamePairingFunction)
							{
								return Options.NamePairingFunction(VertexToAdd.Name, VertexToRemove.Name);
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
						PairedOutputs.Add(FVertexPair{ OutputsToRemove[RemoveIndex], OutputsToAdd[AddIndex].Key });
						OutputsToRemove.RemoveAtSwap(RemoveIndex);
						OutputsToAdd.RemoveAtSwap(AddIndex);
					}
				}
			}

		private:
			bool AddMissingVertices(FMetaSoundFrontendDocumentBuilder& OutBuilder) const
			{
				if (!InputsToAdd.IsEmpty() || !OutputsToAdd.IsEmpty())
				{
					for (const FInputInterfacePair& Pair: InputsToAdd)
					{
						OutBuilder.AddGraphInput(Pair.Key);
					}

					for (const FOutputInterfacePair& Pair : OutputsToAdd)
					{
						OutBuilder.AddGraphOutput(Pair.Key);
					}

					return true;
				}

				return false;
			}

			bool RemoveUnsupportedVertices(FMetaSoundFrontendDocumentBuilder& OutBuilder) const
			{
				if (!InputsToRemove.IsEmpty() || !OutputsToRemove.IsEmpty())
				{
					auto RemoveMemberNodes = [&OutBuilder](
						const FMetasoundFrontendClassVertex& ToRemove,
						TFunctionRef<const FMetasoundFrontendClassVertex*(FName)> FindVertexFunc,
						TFunctionRef<const FMetasoundFrontendNode* (FName)> FindNodeFunc)
					{
						if (const FMetasoundFrontendClassVertex* ClassInput = FindVertexFunc(ToRemove.Name))
						{
							if (FMetasoundFrontendClassVertex::IsFunctionalEquivalent(*ClassInput, ToRemove))
							{
								if (const FMetasoundFrontendNode* Node = FindNodeFunc(ToRemove.Name))
								{
									const bool bNodeRemoved = OutBuilder.RemoveNode(Node->GetID());
									checkf(bNodeRemoved, TEXT("Failed to remove member while attempting to modify MetaSound graph interfaces"));
								}
							}
						}
					};

					// Remove unsupported inputs
					for (const FMetasoundFrontendClassVertex& InputToRemove : InputsToRemove)
					{
						RemoveMemberNodes(InputToRemove,
							[&OutBuilder](FName Name) { return OutBuilder.FindGraphInput(Name); },
							[&OutBuilder](FName Name) { return OutBuilder.FindGraphInputNode(Name); });
					}

					// Remove unsupported outputs
					for (const FMetasoundFrontendClassVertex& OutputToRemove : OutputsToRemove)
					{
						RemoveMemberNodes(OutputToRemove,
							[&OutBuilder](FName Name) { return OutBuilder.FindGraphOutput(Name); },
							[&OutBuilder](FName Name) { return OutBuilder.FindGraphOutputNode(Name); });
					}

					return true;
				}

				return false;
			}

			bool SwapPairedVertices(FMetaSoundFrontendDocumentBuilder& OutBuilder) const
			{
				bool bDidEdit = false;
				for (const FVertexPair& PairedInput : PairedInputs)
				{
					bDidEdit |= OutBuilder.SwapGraphInput(PairedInput.Get<0>(), PairedInput.Get<1>());
				}

				for (const FVertexPair& PairedOutput : PairedOutputs)
				{
					bDidEdit |= OutBuilder.SwapGraphOutput(PairedOutput.Get<0>(), PairedOutput.Get<1>());
				}

				return bDidEdit;
			}

#if WITH_EDITORONLY_DATA
			void UpdateAddedVertexNodePositions(
				EMetasoundFrontendClassType ClassType,
				const FMetaSoundFrontendDocumentBuilder& InBuilder,
				TSet<FName>& AddedNames,
				TFunctionRef<int32(const FVertexName&)> InGetSortOrder,
				TArrayView<FMetasoundFrontendNode> OutNodes)
			{
				// Add graph member nodes by sort order
				TSortedMap<int32, FMetasoundFrontendNode*> SortOrderToNode;
				for (FMetasoundFrontendNode& Node : OutNodes)
				{
					if (const FMetasoundFrontendClass* Class = InBuilder.FindDependency(Node.ClassID))
					{
						if (Class->Metadata.GetType() == ClassType)
						{
							const int32 Index = InGetSortOrder(Node.Name);
							SortOrderToNode.Add(Index, &Node);
						}
					}
				}

				// Prime the first location as an offset prior to an existing location (as provided by a swapped member)
				//  to avoid placing away from user's active area if possible.
				FVector2D NextLocation = { 0.0f, 0.0f };
				{
					int32 NumBeforeDefined = 1;
					for (const TPair<int32, FMetasoundFrontendNode*>& Pair : SortOrderToNode)
					{
						const FMetasoundFrontendNode* Node = Pair.Value;
						const FName NodeName = Node->Name;
						if (AddedNames.Contains(NodeName))
						{
							NumBeforeDefined++;
						}
						else
						{
							const TMap<FGuid, FVector2D>& Locations = Node->Style.Display.Locations;
							if (!Locations.IsEmpty())
							{
								for (const TPair<FGuid, FVector2D>& Location : Locations)
								{
									NextLocation = Location.Value - (NumBeforeDefined * DisplayStyle::NodeLayout::DefaultOffsetY);
									break;
								}
								break;
							}
						}
					}
				}

				// Iterate through sorted map in sequence, slotting in new locations after existing swapped nodes with predefined locations.
				for (TPair<int32, FMetasoundFrontendNode*>& Pair : SortOrderToNode)
				{
					FMetasoundFrontendNode* Node = Pair.Value;
					const FName NodeName = Node->Name;
					if (AddedNames.Contains(NodeName))
					{
						Node->Style.Display.Locations.Add(FGuid(), NextLocation);
						NextLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
					}
					else
					{
						for (const TPair<FGuid, FVector2D>& Location : Node->Style.Display.Locations)
						{
							NextLocation = Location.Value + DisplayStyle::NodeLayout::DefaultOffsetY;
						}
					}
				}
			}
#endif // WITH_EDITORONLY_DATA

		public:
			bool Execute(FMetaSoundFrontendDocumentBuilder& OutBuilder, FMetasoundFrontendDocument& OutDoc)
			{
				bool bDidEdit = false;

				for (const FMetasoundFrontendInterface& Interface : Options.InterfacesToRemove)
				{
					if (OutDoc.Interfaces.Remove(Interface.Version) > 0)
					{
						bDidEdit = true;
#if WITH_EDITORONLY_DATA
						OutDoc.Metadata.ModifyContext.AddInterfaceModified(Interface.Version.Name);
#endif // WITH_EDITORONLY_DATA
					}
				}

				for (const FMetasoundFrontendInterface& Interface : Options.InterfacesToAdd)
				{
					bool bAlreadyInSet = false;
					OutDoc.Interfaces.Add(Interface.Version, &bAlreadyInSet);
					if (!bAlreadyInSet)
					{
						bDidEdit = true;
#if WITH_EDITORONLY_DATA
						OutDoc.Metadata.ModifyContext.AddInterfaceModified(Interface.Version.Name);
#endif // WITH_EDITORONLY_DATA
					}
				}

				bDidEdit |= RemoveUnsupportedVertices(OutBuilder);
				bDidEdit |= SwapPairedVertices(OutBuilder);
				const bool bAddedVertices = AddMissingVertices(OutBuilder);
				bDidEdit |= bAddedVertices;

				if (bDidEdit)
				{
					OutBuilder.RemoveUnusedDependencies();
				}

#if WITH_EDITORONLY_DATA
				if (bAddedVertices && Options.bSetDefaultNodeLocations)
				{
					TArray<FMetasoundFrontendNode>& Nodes = OutDoc.RootGraph.Graph.Nodes;
					// Sort/Place Inputs
					{
						TSet<FName> NamesToSort;
						Algo::Transform(InputsToAdd, NamesToSort, [](const FInputInterfacePair& Pair) { return Pair.Key.Name; });
						auto GetInputSortOrder = [&OutBuilder](const FVertexName& InVertexName)
						{
							const FMetasoundFrontendClassInput* Input = OutBuilder.FindGraphInput(InVertexName);
							checkf(Input, TEXT("Input must exist by this point of modifying the document's interfaces and respective members"));
							return Input->Metadata.SortOrderIndex;
						};
						UpdateAddedVertexNodePositions(EMetasoundFrontendClassType::Input, OutBuilder, NamesToSort, GetInputSortOrder, Nodes);
					}

					// Sort/Place Outputs
					{
						TSet<FName> NamesToSort;
						Algo::Transform(OutputsToAdd, NamesToSort, [](const FOutputInterfacePair& OutputInterfacePair) { return OutputInterfacePair.Key.Name; });
						auto GetOutputSortOrder = [&OutBuilder](const FVertexName& InVertexName)
						{
							const FMetasoundFrontendClassOutput* Output = OutBuilder.FindGraphOutput(InVertexName);
							checkf(Output, TEXT("Output must exist by this point of modifying the document's interfaces and respective members"));
							return Output->Metadata.SortOrderIndex;
						};
						UpdateAddedVertexNodePositions(EMetasoundFrontendClassType::Output, OutBuilder, NamesToSort, GetOutputSortOrder, Nodes);
					}
				}
#endif // WITH_EDITORONLY_DATA

				return bDidEdit;
			}

			const FModifyInterfaceOptions Options;

			using FVertexPair = TTuple<FMetasoundFrontendClassVertex, FMetasoundFrontendClassVertex>;
			TArray<FVertexPair> PairedInputs;
			TArray<FVertexPair> PairedOutputs;

			using FInputInterfacePair = TPair<FMetasoundFrontendClassInput, const FMetasoundFrontendInterface*>;
			using FOutputInterfacePair = TPair<FMetasoundFrontendClassOutput, const FMetasoundFrontendInterface*>;
			TArray<FInputInterfacePair> InputsToAdd;
			TArray<FOutputInterfacePair> OutputsToAdd;

			TArray<FMetasoundFrontendClassInput> InputsToRemove;
			TArray<FMetasoundFrontendClassOutput> OutputsToRemove;
		};
	} // namespace DocumentBuilderPrivate

	FModifyInterfaceOptions::FModifyInterfaceOptions(const TArray<FMetasoundFrontendInterface>& InInterfacesToRemove, const TArray<FMetasoundFrontendInterface>& InInterfacesToAdd)
		: InterfacesToRemove(InInterfacesToRemove)
		, InterfacesToAdd(InInterfacesToAdd)
	{
	}

	FModifyInterfaceOptions::FModifyInterfaceOptions(const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToRemove, const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToAdd)
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
	}
} // namespace Metasound::Frontend


FMetaSoundFrontendDocumentBuilder::FMetaSoundFrontendDocumentBuilder(TScriptInterface<IMetaSoundDocumentInterface> InDocumentInterface)
	: DocumentInterface(InDocumentInterface)
{
	if (DocumentInterface)
	{
		ReloadCache();
	}
}

FMetaSoundFrontendDocumentBuilder::FMetaSoundFrontendDocumentBuilder(const FMetaSoundFrontendDocumentBuilder& InBuilder)
{
	DocumentInterface = InBuilder.DocumentInterface;
	if (DocumentInterface)
	{
		ReloadCache();
	}
}

FMetaSoundFrontendDocumentBuilder::FMetaSoundFrontendDocumentBuilder(FMetaSoundFrontendDocumentBuilder&& InBuilder)
	: DocumentInterface(MoveTemp(InBuilder.DocumentInterface))
	, DocumentCache(MoveTemp(InBuilder.DocumentCache))
{
}

FMetaSoundFrontendDocumentBuilder& FMetaSoundFrontendDocumentBuilder::operator=(FMetaSoundFrontendDocumentBuilder&& InRHS)
{
	using namespace Metasound::Frontend;
	DocumentInterface = MoveTemp(InRHS.DocumentInterface);
	DocumentCache = MoveTemp(InRHS.DocumentCache);
	return *this;
}

FMetaSoundFrontendDocumentBuilder& FMetaSoundFrontendDocumentBuilder::operator=(const FMetaSoundFrontendDocumentBuilder& InRHS)
{
	using namespace Metasound::Frontend;

	DocumentInterface = InRHS.DocumentInterface;
	if (DocumentInterface)
	{
		ReloadCache();
	}
	return *this;
}

const FMetasoundFrontendClass* FMetaSoundFrontendDocumentBuilder::AddGraphDependency(const FMetasoundFrontendGraphClass& InClass)
{
	using namespace Metasound::Frontend;

	checkf(InClass.Metadata.GetType() == EMetasoundFrontendClassType::Graph, TEXT("Call should only be used for 'Graph' type dependencies. Use 'AddNativeDependency' for other class types."));

	FMetasoundFrontendDocument& Document = GetDocument();
	const FMetasoundFrontendClass* Dependency = nullptr;

	FMetasoundFrontendClass NewDependency = InClass;

	// All dependencies are listed as 'External' from the perspective of the owning document.
	// This makes them implementation agnostic to accommodate nativization of assets.
	NewDependency.Metadata.SetType(EMetasoundFrontendClassType::External);
	NewDependency.ID = FGuid::NewGuid();
	Dependency = &Document.Dependencies.Emplace_GetRef(MoveTemp(NewDependency));

	const int32 NewIndex = Document.Dependencies.Num() - 1;
	DocumentCache->OnDependencyAdded(NewIndex);

	return Dependency;
}

const FMetasoundFrontendClass* FMetaSoundFrontendDocumentBuilder::AddNativeDependency(const FMetasoundFrontendClassMetadata& InClassMetadata)
{
	using namespace Metasound::Frontend;

	checkf(InClassMetadata.GetType() != EMetasoundFrontendClassType::Graph, TEXT("Graph dependencies must be added via 'AddGraphDependency' to avoid overhead of registration where not necessary"));

	FMetasoundFrontendDocument& Document = GetDocument();
	const FMetasoundFrontendClass* Dependency = nullptr;

	const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(InClassMetadata);
	FMetasoundFrontendClass NewDependency = GenerateClass(RegistryKey);
	NewDependency.ID = FGuid::NewGuid();
	Dependency = &Document.Dependencies.Emplace_GetRef(MoveTemp(NewDependency));

	const int32 NewIndex = Document.Dependencies.Num() - 1;
	DocumentCache->OnDependencyAdded(NewIndex);

	return Dependency;
}

const FMetasoundFrontendEdge* FMetaSoundFrontendDocumentBuilder::AddEdge(FMetasoundFrontendEdge&& InNewEdge)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
	if (CanAddEdge(InNewEdge))
	{
		IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache();

		const FMetasoundFrontendEdge& NewEdge = Graph.Edges.Add_GetRef(MoveTemp(InNewEdge));
		const int32 NewIndex = Graph.Edges.Num() - 1;
		EdgeCache.OnEdgeAdded(NewIndex);
		return &NewEdge;
	}

	return nullptr;
}

bool FMetaSoundFrontendDocumentBuilder::AddNamedEdges(const TSet<Metasound::Frontend::FNamedEdge>& EdgesToMake, TArray<const FMetasoundFrontendEdge*>* OutNewEdges)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	if (OutNewEdges)
	{
		OutNewEdges->Reset();
	}

	bool bSuccess = true;

	TArray<FMetasoundFrontendEdge> EdgesToAdd;
	for (const FNamedEdge& Edge : EdgesToMake)
	{
		const FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
		const FMetasoundFrontendVertex* OutputVertex = NodeCache.FindOutputVertex(Edge.OutputNodeID, Edge.OutputName);
		const FMetasoundFrontendVertex* InputVertex = NodeCache.FindInputVertex(Edge.InputNodeID, Edge.InputName);

		if (OutputVertex && InputVertex)
		{
			FMetasoundFrontendEdge NewEdge = { Edge.OutputNodeID, OutputVertex->VertexID, Edge.InputNodeID, InputVertex->VertexID };
			if (CanAddEdge(NewEdge))
			{
				EdgesToAdd.Add(MoveTemp(NewEdge));
			}
			else
			{
				bSuccess = false;
				UE_LOG(LogMetaSound, Warning, TEXT("Failed to add connections between MetaSound output '%s' and input '%s': Vertex types either incompatable (eg. construction pin to non-construction pin) or input already connected."), *Edge.OutputName.ToString(), *Edge.InputName.ToString());
			}
		}
	}

	for (FMetasoundFrontendEdge& EdgeToAdd : EdgesToAdd)
	{
		const FMetasoundFrontendEdge* NewEdge = AddEdge(MoveTemp(EdgeToAdd));
		if (ensureAlwaysMsgf(NewEdge, TEXT("Failed to add MetaSound graph edge via DocumentBuilder when prior step validated edge add was valid")))
		{
			if (OutNewEdges)
			{
				OutNewEdges->Add(NewEdge);
			}
		}
		else
		{
			bSuccess = false;
		}
	}

	return bSuccess;
}

bool FMetaSoundFrontendDocumentBuilder::AddEdgesByNodeClassInterfaceBindings(const FGuid& InFromNodeID, const FGuid& InToNodeID)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	const FMetasoundFrontendDocument* FromNodeDocument = FindOrLoadNodeClassDocument(InFromNodeID);
	const FMetasoundFrontendDocument* ToNodeDocument = FindOrLoadNodeClassDocument(InToNodeID);
	if (FromNodeDocument && ToNodeDocument)
	{
		TSet<FNamedEdge> NamedEdges;
		if (DocumentBuilderPrivate::TryGetInterfaceBoundEdges(InFromNodeID, *FromNodeDocument, InToNodeID, *ToNodeDocument, NamedEdges))
		{
			return AddNamedEdges(NamedEdges);
		}
	}

	return false;

}

bool FMetaSoundFrontendDocumentBuilder::AddEdgesFromMatchingInterfaceNodeOutputsToGraphOutputs(const FGuid& InNodeID, TArray<const FMetasoundFrontendEdge*>& OutEdgesCreated)
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::AddEdgesFromMatchingInterfaceNodeOutputsToGraphOutputs);

	using namespace Metasound::Frontend;

	OutEdgesCreated.Reset();

	const FMetasoundFrontendDocument* NodeDocument = FindOrLoadNodeClassDocument(InNodeID);
	if (!NodeDocument)
	{
		return false;
	}

	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	const TSet<FMetasoundFrontendVersion>& NodeInterfaces = NodeDocument->Interfaces;
	const TSet<FMetasoundFrontendVersion> CommonInterfaces = NodeInterfaces.Intersect(GetDocument().Interfaces);

	TSet<FNamedEdge> EdgesToMake;
	for (const FMetasoundFrontendVersion& Version : CommonInterfaces)
	{
		const FInterfaceRegistryKey InterfaceKey = GetInterfaceRegistryKey(Version);
		if (const IInterfaceRegistryEntry* RegistryEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(InterfaceKey))
		{
			Algo::Transform(RegistryEntry->GetInterface().Outputs, EdgesToMake, [this, &NodeCache, InNodeID](const FMetasoundFrontendClassOutput& Output)
			{
				const FMetasoundFrontendGraph& Graph = GetDocument().RootGraph.Graph;
				const FMetasoundFrontendVertex* NodeVertex = NodeCache.FindOutputVertex(InNodeID, Output.Name);
				check(NodeVertex);
				const FMetasoundFrontendNode* OutputNode = NodeCache.FindOutputNode(Output.Name);
				check(OutputNode);
				const TArray<FMetasoundFrontendVertex>& Inputs = OutputNode->Interface.Inputs;
				check(!Inputs.IsEmpty());
				return FNamedEdge { InNodeID, NodeVertex->Name, OutputNode->GetID(), Inputs.Last().Name };
			});
		}
	}

	return AddNamedEdges(EdgesToMake, &OutEdgesCreated);
}

bool FMetaSoundFrontendDocumentBuilder::AddEdgesFromMatchingInterfaceNodeInputsToGraphInputs(const FGuid& InNodeID, TArray<const FMetasoundFrontendEdge*>& OutEdgesCreated)
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::AddEdgesFromMatchingInterfaceNodeInputsToGraphInputs);

	using namespace Metasound::Frontend;

	OutEdgesCreated.Reset();

	const FMetasoundFrontendDocument* NodeDocument = FindOrLoadNodeClassDocument(InNodeID);
	if (!NodeDocument)
	{
		return false;
	}

	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	const TSet<FMetasoundFrontendVersion>& NodeInterfaces = NodeDocument->Interfaces;
	const TSet<FMetasoundFrontendVersion> CommonInterfaces = NodeInterfaces.Intersect(GetDocument().Interfaces);

	TSet<FNamedEdge> EdgesToMake;
	for (const FMetasoundFrontendVersion& Version : CommonInterfaces)
	{
		const FInterfaceRegistryKey InterfaceKey = GetInterfaceRegistryKey(Version);
		if (const IInterfaceRegistryEntry* RegistryEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(InterfaceKey))
		{
			Algo::Transform(RegistryEntry->GetInterface().Inputs, EdgesToMake, [this, &NodeCache, InNodeID](const FMetasoundFrontendClassInput& Input)
			{
				const FMetasoundFrontendGraph& Graph = GetDocument().RootGraph.Graph;
				const FMetasoundFrontendVertex* NodeVertex = NodeCache.FindOutputVertex(InNodeID, Input.Name);
				check(NodeVertex);
				const FMetasoundFrontendNode* InputNode = NodeCache.FindInputNode(Input.Name);
				check(InputNode);
				const TArray<FMetasoundFrontendVertex>& Outputs = InputNode->Interface.Outputs;
				check(!Outputs.IsEmpty());
				return FNamedEdge { InNodeID, NodeVertex->Name, InputNode->GetID(), Outputs.Last().Name };
			});
		}
	}

	return AddNamedEdges(EdgesToMake, &OutEdgesCreated);
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::AddGraphInput(const FMetasoundFrontendClassInput& InClassInput)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;

	checkf(InClassInput.NodeID.IsValid(), TEXT("Unassigned NodeID when adding graph input"));
	checkf(InClassInput.VertexID.IsValid(), TEXT("Unassigned VertexID when adding graph input"));

	if (InClassInput.TypeName.IsNone())
	{
		UE_LOG(LogMetaSound, Error, TEXT("TypeName unset when attempting to add class input '%s'"), *InClassInput.Name.ToString());
		return nullptr;
	}
	else if (const FMetasoundFrontendNode* Node = DocumentCache->GetNodeCache().FindInputNode(InClassInput.Name))
	{
		UE_LOG(LogMetaSound, Error, TEXT("Attempting to add MetaSound graph input '%s' when input with name already exists"), *InClassInput.Name.ToString());
		return Node;
	}

	auto FindRegistryClass = [&InClassInput](FMetasoundFrontendClass& OutClass) -> bool
	{
		switch (InClassInput.AccessType)
		{
		case EMetasoundFrontendVertexAccessType::Value:
		{
			return IDataTypeRegistry::Get().GetFrontendConstructorInputClass(InClassInput.TypeName, OutClass);
		}
		break;

		case EMetasoundFrontendVertexAccessType::Reference:
		{
			return IDataTypeRegistry::Get().GetFrontendInputClass(InClassInput.TypeName, OutClass);
		}
		break;

		case EMetasoundFrontendVertexAccessType::Unset:
		default:
		{
			checkNoEntry();
		}
		break;
		}

		return false;
	};

	FMetasoundFrontendClass Class;
	if (FindRegistryClass(Class))
	{
		if(!FindDependency(Class.Metadata))
		{
			AddNativeDependency(Class.Metadata);
		}

		auto FinalizeNode = [&InClassInput](FMetasoundFrontendNode& InOutNode, const Metasound::Frontend::FNodeRegistryKey&)
		{
			DocumentBuilderPrivate::FinalizeGraphVertexNode(InOutNode, InClassInput);
		};
		if (FMetasoundFrontendNode* NewNode = AddNodeInternal(Class.Metadata, FinalizeNode, InClassInput.NodeID))
		{
			FMetasoundFrontendClassInput& NewInput = RootGraph.Interface.Inputs.Add_GetRef(InClassInput);
			if (!NewInput.VertexID.IsValid())
			{
				NewInput.VertexID = FGuid::NewGuid();
			}

#if WITH_EDITORONLY_DATA
			Document.Metadata.ModifyContext.AddMemberIDModified(InClassInput.NodeID);
#endif // WITH_EDITORONLY_DATA

			return NewNode;
		}
	}

	return nullptr;
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::AddGraphOutput(const FMetasoundFrontendClassOutput& InClassOutput)
{
	using namespace Metasound::Frontend;

	checkf(InClassOutput.NodeID.IsValid(), TEXT("Unassigned NodeID when adding graph output"));
	checkf(InClassOutput.VertexID.IsValid(), TEXT("Unassigned VertexID when adding graph output"));

	FMetasoundFrontendDocument& Document = GetDocument();
	FMetasoundFrontendGraphClass& RootGraph = Document.RootGraph;

	if (InClassOutput.TypeName.IsNone())
	{
		UE_LOG(LogMetaSound, Error, TEXT("TypeName unset when attempting to add class output '%s'"), *InClassOutput.Name.ToString());
		return nullptr;
	}
	else if (const FMetasoundFrontendNode* Node = DocumentCache->GetNodeCache().FindInputNode(InClassOutput.Name))
	{
		UE_LOG(LogMetaSound, Error, TEXT("Attempting to add MetaSound graph output '%s' when output with name already exists"), *InClassOutput.Name.ToString());
		return Node;
	}

	auto FindRegistryClass = [&InClassOutput](FMetasoundFrontendClass& OutClass) -> bool
	{
		switch (InClassOutput.AccessType)
		{
		case EMetasoundFrontendVertexAccessType::Value:
		{
			return IDataTypeRegistry::Get().GetFrontendConstructorOutputClass(InClassOutput.TypeName, OutClass);
		}
		break;

		case EMetasoundFrontendVertexAccessType::Reference:
		{
			return IDataTypeRegistry::Get().GetFrontendOutputClass(InClassOutput.TypeName, OutClass);
		}
		break;

		case EMetasoundFrontendVertexAccessType::Unset:
		default:
		{
			checkNoEntry();
		}
		break;
		}

		return false;
	};

	FMetasoundFrontendClass Class;
	if (FindRegistryClass(Class))
	{
		if (!FindDependency(Class.Metadata))
		{
			AddNativeDependency(Class.Metadata);
		}

		auto FinalizeNode = [&InClassOutput](FMetasoundFrontendNode& InOutNode, const Metasound::Frontend::FNodeRegistryKey&)
		{
			DocumentBuilderPrivate::FinalizeGraphVertexNode(InOutNode, InClassOutput);
		};
		if (FMetasoundFrontendNode* NewNode = AddNodeInternal(Class.Metadata, FinalizeNode, InClassOutput.NodeID))
		{
			FMetasoundFrontendClassOutput& NewOutput = RootGraph.Interface.Outputs.Add_GetRef(InClassOutput);
			if (!NewOutput.VertexID.IsValid())
			{
				NewOutput.VertexID = FGuid::NewGuid();
			}

#if WITH_EDITORONLY_DATA
			Document.Metadata.ModifyContext.AddMemberIDModified(InClassOutput.NodeID);
#endif // WITH_EDITORONLY_DATA

			return NewNode;
		}
	}

	return nullptr;
}

bool FMetaSoundFrontendDocumentBuilder::AddInterface(FName InterfaceName)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendInterface Interface;
	if (ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceName, Interface))
	{
		if (GetDocument().Interfaces.Contains(Interface.Version))
		{
			UE_LOG(LogMetaSound, VeryVerbose, TEXT("MetaSound interface '%s' already found on document. MetaSoundBuilder skipping add request."), *InterfaceName.ToString());
			return true;
		}

		const FTopLevelAssetPath BuilderClassPath = GetBuilderClassPath();
		const FInterfaceRegistryKey Key = GetInterfaceRegistryKey(Interface.Version);
		if (const IInterfaceRegistryEntry* Entry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(Key))
		{
			const FMetasoundFrontendInterfaceUClassOptions* ClassOptions = Entry->GetInterface().FindClassOptions(BuilderClassPath);
			if (ClassOptions && !ClassOptions->bIsModifiable)
			{
				UE_LOG(LogMetaSound, Error, TEXT("DocumentBuilder failed to add MetaSound Interface '%s' to document: is not set to be modifiable for given UClass '%s'"), *InterfaceName.ToString(), *BuilderClassPath.ToString());
				return false;
			}
		}

		const FModifyRootGraphInterfaces ModifyInterfaces({ }, { Interface.Version });
		if (ModifyInterfaces.Transform(GetDocument()))
		{
			// TODO: Add ability to update local builder caches dynamically
			// in ModifyRootGraphInterfaces transform once it moves away
			// from using controllers internally. Then cache updates can
			// be more isolated and not use this full rebuild call.
			ReloadCache();
			return true;
		}
	}

	return false;
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::AddGraphNode(const FMetasoundFrontendGraphClass& InGraphClass, FGuid InNodeID)
{
	auto FinalizeNode = [](const FMetasoundFrontendNode& Node, const Metasound::Frontend::FNodeRegistryKey& ClassKey)
	{
#if WITH_EDITOR
		using namespace Metasound::Frontend;

		// Cache the asset name on the node if it node is reference to asset-defined graph.
		if (IMetaSoundAssetManager* AssetManager = IMetaSoundAssetManager::Get())
		{
			if (const FSoftObjectPath* Path = AssetManager->FindObjectPathFromKey(ClassKey))
			{
				return FName(*Path->GetAssetName());
			}
		}
#endif // WITH_EDITOR

		return Node.Name;
	};


	// Dependency is considered "External" when looked up or added on another graph
	FMetasoundFrontendClassMetadata NewClassMetadata = InGraphClass.Metadata;
	NewClassMetadata.SetType(EMetasoundFrontendClassType::External);

	if (!FindDependency(NewClassMetadata))
	{
		AddGraphDependency(InGraphClass);
	}

	return AddNodeInternal(NewClassMetadata, FinalizeNode, InNodeID);
}

FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::AddNodeInternal(const FMetasoundFrontendClassMetadata& InClassMetadata, FFinalizeNodeFunctionRef FinalizeNode, FGuid InNodeID)
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::AddNodeInternal);

	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	const FNodeRegistryKey ClassKey = NodeRegistryKey::CreateKey(InClassMetadata);
	if (const FMetasoundFrontendClass* Dependency = DocumentCache->FindDependency(ClassKey))
	{
		FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
		TArray<FMetasoundFrontendNode>& Nodes = Graph.Nodes;
		FMetasoundFrontendNode& Node = Nodes.Emplace_GetRef(*Dependency);
		Node.UpdateID(InNodeID);
		FinalizeNode(Node, ClassKey);

		const int32 NewIndex = Nodes.Num() - 1;
		IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
		NodeCache.OnNodeAdded(NewIndex);

#if WITH_EDITORONLY_DATA
		Document.Metadata.ModifyContext.AddNodeIDModified(InNodeID);
#endif // WITH_EDITORONLY_DATA
		return &Node;
	}

	return nullptr;
}

bool FMetaSoundFrontendDocumentBuilder::CanAddEdge(const FMetasoundFrontendEdge& InEdge) const
{
	using namespace Metasound::Frontend;

	const FMetasoundFrontendDocument& Document = GetDocument();
	const IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache();

	if (!EdgeCache.IsNodeInputConnected(InEdge.ToNodeID, InEdge.ToVertexID))
	{
		const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
		const FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
		const FMetasoundFrontendVertex* OutputVertex = NodeCache.FindOutputVertex(InEdge.FromNodeID, InEdge.FromVertexID);
		const FMetasoundFrontendVertex* InputVertex = NodeCache.FindInputVertex(InEdge.ToNodeID, InEdge.ToVertexID);
		if (OutputVertex && InputVertex)
		{
			if (OutputVertex->TypeName == InputVertex->TypeName)
			{
				const FMetasoundFrontendClassOutput* ClassOutput = FindNodeOutputClassOutput(InEdge.FromNodeID, InEdge.FromVertexID);
				const FMetasoundFrontendClassInput* ClassInput = FindNodeInputClassInput(InEdge.ToNodeID, InEdge.ToVertexID);
				if (ClassInput && ClassOutput)
				{
					return FMetasoundFrontendClassVertex::CanConnectVertexAccessTypes(ClassOutput->AccessType, ClassInput->AccessType);
				}
			}
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveUnusedDependencies()
{
	bool bDidEdit = false;

	TArray<FMetasoundFrontendClass>& Dependencies = GetDocument().Dependencies;
	for (int32 i = Dependencies.Num() - 1; i >= 0; --i)
	{
		const FGuid& ClassID = Dependencies[i].ID;
		if (!DocumentCache->GetNodeCache().ContainsNodesOfClassID(ClassID))
		{
			checkf(RemoveDependency(ClassID), TEXT("Failed to remove dependency that was found on document and was not referenced by nodes"));
			bDidEdit = true;
		}
	}

	return bDidEdit;
}

void FMetaSoundFrontendDocumentBuilder::ClearGraph()
{
	FMetasoundFrontendGraphClass& GraphClass = GetDocument().RootGraph;
	GraphClass.Graph.Nodes.Reset();
	GraphClass.Graph.Edges.Reset();
	GraphClass.Interface.Inputs.Reset();
	GraphClass.Interface.Outputs.Reset();
	GraphClass.PresetOptions.InputsInheritingDefault.Reset();
	GetDocument().Interfaces.Reset();
	RemoveUnusedDependencies();
	ReloadCache();
}

bool FMetaSoundFrontendDocumentBuilder::ContainsEdge(const FMetasoundFrontendEdge& InEdge) const
{
	using namespace Metasound::Frontend;
	IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache();
	return EdgeCache.ContainsEdge(InEdge);
}

bool FMetaSoundFrontendDocumentBuilder::ContainsNode(const FGuid& InNodeID) const
{
	using namespace Metasound::Frontend;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
	return NodeCache.ContainsNode(InNodeID);
}

bool FMetaSoundFrontendDocumentBuilder::ConvertFromPreset()
{
	using namespace Metasound::Frontend;

	if (IsPreset())
	{
		FMetasoundFrontendDocument& Document = GetDocument();
		FMetasoundFrontendGraphClass& RootGraphClass = Document.RootGraph;
		FMetasoundFrontendGraphClassPresetOptions& PresetOptions = RootGraphClass.PresetOptions;
		PresetOptions.bIsPreset = false;

#if WITH_EDITOR
		FMetasoundFrontendGraphStyle& Style = Document.RootGraph.Graph.Style;
		Style.bIsGraphEditable = true;
#endif // WITH_EDITOR

		return true;
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::ConvertToPreset(const FMetasoundFrontendDocument& InReferencedDocument)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	ClearGraph();

	FMetasoundFrontendGraphClass& PresetAssetRootGraph = GetDocument().RootGraph;
	FMetasoundFrontendGraph& PresetAssetGraph = PresetAssetRootGraph.Graph;
	// Mark preset as auto-update and non-editable
#if WITH_EDITORONLY_DATA
	PresetAssetGraph.Style.bIsGraphEditable = false;
#endif // WITH_EDITORONLY_DATA

	// Mark all inputs as inherited by default
	TArray<FName> InputsInheritingDefault;
	Algo::Transform(PresetAssetRootGraph.Interface.Inputs, InputsInheritingDefault, [](const FMetasoundFrontendClassInput& Input)
	{
		return Input.Name;
	});

	PresetAssetRootGraph.PresetOptions.bIsPreset = true;
	PresetAssetRootGraph.PresetOptions.InputsInheritingDefault = TSet<FName>(InputsInheritingDefault);

	// Apply root graph transform 
	FRebuildPresetRootGraph RebuildPresetRootGraph(InReferencedDocument);
	if (RebuildPresetRootGraph.Transform(GetDocument()))
	{
		ReloadCache();
		return true;
	}
	return false;
}

bool FMetaSoundFrontendDocumentBuilder::FindDeclaredInterfaces(TArray<const Metasound::Frontend::IInterfaceRegistryEntry*>& OutInterfaces) const
{
	return FindDeclaredInterfaces(GetDocument(), OutInterfaces);
}

bool FMetaSoundFrontendDocumentBuilder::FindDeclaredInterfaces(const FMetasoundFrontendDocument& InDocument, TArray<const Metasound::Frontend::IInterfaceRegistryEntry*>& OutInterfaces)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	bool bInterfacesFound = true;

	Algo::Transform(InDocument.Interfaces, OutInterfaces, [&bInterfacesFound](const FMetasoundFrontendVersion& Version)
	{
		const FInterfaceRegistryKey InterfaceKey = GetInterfaceRegistryKey(Version);
		const IInterfaceRegistryEntry* RegistryEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(InterfaceKey);
		if (!RegistryEntry)
		{
			bInterfacesFound = false;
			UE_LOG(LogMetaSound, Warning, TEXT("No registered interface matching interface version on document [InterfaceVersion:%s]"), *Version.ToString());
		}

		return RegistryEntry;
	});

	return bInterfacesFound;
}

const FMetasoundFrontendClass* FMetaSoundFrontendDocumentBuilder::FindDependency(const FGuid& InClassID) const
{
	return DocumentCache->FindDependency(InClassID);
}

const FMetasoundFrontendClass* FMetaSoundFrontendDocumentBuilder::FindDependency(const FMetasoundFrontendClassMetadata& InMetadata) const
{
	using namespace Metasound::Frontend;

	checkf(InMetadata.GetType() != EMetasoundFrontendClassType::Graph,
		TEXT("Dependencies are never listed as 'Graph' types. Graphs are considered 'External' from the perspective of the parent document to allow for nativization."));
	const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(InMetadata);
	return DocumentCache->FindDependency(RegistryKey);
}

bool FMetaSoundFrontendDocumentBuilder::FindInterfaceInputNodes(FName InterfaceName, TArray<const FMetasoundFrontendNode*>& OutInputs) const
{
	using namespace Metasound::Frontend;

	OutInputs.Reset();

	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	FMetasoundFrontendInterface Interface;
	if (ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceName, Interface))
	{
		if (GetDocument().Interfaces.Contains(Interface.Version))
		{
			TArray<const FMetasoundFrontendNode*> InterfaceInputs;
			for (const FMetasoundFrontendClassInput& Input : Interface.Inputs)
			{
				if (const int32* NodeIndex = NodeCache.FindInputNodeIndex(Input.Name))
				{
					const FMetasoundFrontendNode& Node = GetDocument().RootGraph.Graph.Nodes[*NodeIndex];
					InterfaceInputs.Add(&Node);
				}
				else
				{
					return false;
				}
			}

			OutInputs = MoveTemp(InterfaceInputs);
			return true;
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::FindInterfaceOutputNodes(FName InterfaceName, TArray<const FMetasoundFrontendNode*>& OutOutputs) const
{
	using namespace Metasound::Frontend;

	OutOutputs.Reset();

	FMetasoundFrontendInterface Interface;
	if (ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceName, Interface))
	{
		if (GetDocument().Interfaces.Contains(Interface.Version))
		{
			TArray<const FMetasoundFrontendNode*> InterfaceOutputs;
			for (const FMetasoundFrontendClassOutput& Output : Interface.Outputs)
			{
				if (const int32* NodeIndex = DocumentCache->GetNodeCache().FindOutputNodeIndex(Output.Name))
				{
					const FMetasoundFrontendNode& Node = GetDocument().RootGraph.Graph.Nodes[*NodeIndex];
					InterfaceOutputs.Add(&Node);
				}
				else
				{
					return false;
				}
			}

			OutOutputs = MoveTemp(InterfaceOutputs);
			return true;
		}
	}

	return false;
}

const FMetasoundFrontendClassInput* FMetaSoundFrontendDocumentBuilder::FindGraphInput(FName InputName) const
{
	const TArray<FMetasoundFrontendClassInput>& Inputs = GetDocument().RootGraph.Interface.Inputs;
	return Inputs.FindByPredicate([&InputName](const FMetasoundFrontendClassInput& Input) { return Input.Name == InputName; });
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::FindGraphInputNode(FName InputName) const
{
	using namespace Metasound::Frontend;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
	return NodeCache.FindInputNode(InputName);
}

const FMetasoundFrontendClassOutput* FMetaSoundFrontendDocumentBuilder::FindGraphOutput(FName OutputName) const
{
	const TArray<FMetasoundFrontendClassOutput>& Outputs = GetDocument().RootGraph.Interface.Outputs;
	return Outputs.FindByPredicate([&OutputName](const FMetasoundFrontendClassOutput& Output) { return Output.Name == OutputName; });
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::FindGraphOutputNode(FName OutputName) const
{
	using namespace Metasound::Frontend;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
	return NodeCache.FindOutputNode(OutputName);
}

const FMetasoundFrontendVertex* FMetaSoundFrontendDocumentBuilder::FindNodeInput(const FGuid& InNodeID, const FGuid& InVertexID) const
{
	using namespace Metasound::Frontend;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
	return NodeCache.FindInputVertex(InNodeID, InVertexID);
}

const FMetasoundFrontendVertex* FMetaSoundFrontendDocumentBuilder::FindNodeInput(const FGuid& InNodeID, FName InVertexName) const
{
	using namespace Metasound::Frontend;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
	return NodeCache.FindInputVertex(InNodeID, InVertexName);
}

const FMetasoundFrontendVertex* FMetaSoundFrontendDocumentBuilder::FindNodeOutput(const FGuid& InNodeID, const FGuid& InVertexID) const
{
	using namespace Metasound::Frontend;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
	return NodeCache.FindOutputVertex(InNodeID, InVertexID);
}

const FMetasoundFrontendVertex* FMetaSoundFrontendDocumentBuilder::FindNodeOutput(const FGuid& InNodeID, FName InVertexName) const
{
	using namespace Metasound::Frontend;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
	return NodeCache.FindOutputVertex(InNodeID, InVertexName);
}

const FMetasoundFrontendClassInput* FMetaSoundFrontendDocumentBuilder::FindNodeInputClassInput(const FGuid& InNodeID, const FGuid& InVertexID) const
{
	using namespace Metasound::Frontend;

	const FMetasoundFrontendDocument& Document = GetDocument();
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	if (const FMetasoundFrontendNode* Node = NodeCache.FindNode(InNodeID))
	{
		if (const FMetasoundFrontendClass* Class = DocumentCache->FindDependency(Node->ClassID))
		{
			auto InputMatchesVertex = [&InVertexID](const FMetasoundFrontendClassVertex& ClassVertex) { return ClassVertex.VertexID == InVertexID; };
			return Class->Interface.Inputs.FindByPredicate(InputMatchesVertex);
		}
	}

	return nullptr;
}

const FMetasoundFrontendClassOutput* FMetaSoundFrontendDocumentBuilder::FindNodeOutputClassOutput(const FGuid& InNodeID, const FGuid& InVertexID) const
{
	using namespace Metasound::Frontend;

	const FMetasoundFrontendDocument& Document = GetDocument();
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	if (const FMetasoundFrontendNode* Node = NodeCache.FindNode(InNodeID))
	{
		if (const FMetasoundFrontendClass* Class = DocumentCache->FindDependency(Node->ClassID))
		{
			auto OutputMatchesVertex = [&InVertexID](const FMetasoundFrontendClassVertex& ClassVertex) { return ClassVertex.VertexID == InVertexID; };
			return Class->Interface.Outputs.FindByPredicate(OutputMatchesVertex);
		}
	}

	return nullptr;
}

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::FindNode(const FGuid& InNodeID) const
{
	using namespace Metasound::Frontend;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
	return NodeCache.FindNode(InNodeID);
}

TArray<const FMetasoundFrontendVertex*> FMetaSoundFrontendDocumentBuilder::FindNodeInputs(const FGuid& InNodeID, FName TypeName) const
{
	return DocumentCache->GetNodeCache().FindNodeInputs(InNodeID, TypeName);
}

TArray<const FMetasoundFrontendVertex*> FMetaSoundFrontendDocumentBuilder::FindNodeOutputs(const FGuid& InNodeID, FName TypeName) const
{
	return DocumentCache->GetNodeCache().FindNodeOutputs(InNodeID, TypeName);
}

const FMetasoundFrontendDocument* FMetaSoundFrontendDocumentBuilder::FindOrLoadNodeClassDocument(const FGuid& InNodeID) const
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	const FMetasoundFrontendDocument& Document = GetDocument();
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
	if (const FMetasoundFrontendNode* Node = NodeCache.FindNode(InNodeID))
	{
		if (const FMetasoundFrontendClass* NodeClass = DocumentCache->FindDependency(Node->ClassID))
		{
			const FNodeRegistryKey NodeClassRegistryKey = NodeRegistryKey::CreateKey(NodeClass->Metadata);
			if (FMetasoundAssetBase* Asset = IMetaSoundAssetManager::GetChecked().TryLoadAssetFromKey(NodeClassRegistryKey))
			{
				UObject* Object = Asset->GetOwningAsset();
				TScriptInterface<IMetaSoundDocumentInterface> Interface = Object;
				check(Interface);
				return &Interface->GetDocument();
			}
		}
	}

	return nullptr;
}

const FTopLevelAssetPath FMetaSoundFrontendDocumentBuilder::GetBuilderClassPath() const
{
	IMetaSoundDocumentInterface* Interface = DocumentInterface.GetInterface();
	checkf(Interface, TEXT("Failed to return class path; interface must always be valid while builder is operating on MetaSound UObject!"));
	return Interface->GetBaseMetaSoundUClass().GetClassPathName();
}

FMetasoundFrontendDocument& FMetaSoundFrontendDocumentBuilder::GetDocument()
{
	IMetaSoundDocumentInterface* Interface = DocumentInterface.GetInterface();
	checkf(Interface, TEXT("Failed to return document; interface must always be valid while builder is operating on MetaSound UObject!"));
	return Interface->GetDocument();
}

const FMetasoundFrontendDocument& FMetaSoundFrontendDocumentBuilder::GetDocument() const
{
	return DocumentInterface->GetDocument();
}

const IMetaSoundDocumentInterface& FMetaSoundFrontendDocumentBuilder::GetDocumentInterface() const
{
	IMetaSoundDocumentInterface* Interface = DocumentInterface.GetInterface();
	check(Interface);
	return *Interface;
}

void FMetaSoundFrontendDocumentBuilder::InitGraphClassMetadata(FMetasoundFrontendClassMetadata& InOutMetadata, bool bResetVersion)
{
	InOutMetadata.SetClassName(FMetasoundFrontendClassName(FName(), *FGuid::NewGuid().ToString(), FName()));

	if (bResetVersion)
	{
		InOutMetadata.SetVersion({ 1, 0 });
	}

	InOutMetadata.SetType(EMetasoundFrontendClassType::Graph);
}

void FMetaSoundFrontendDocumentBuilder::InitDocument()
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::InitDocument);

	FMetasoundFrontendDocument& Document = GetDocument();

	// 1. Set default class Metadata
	{
		constexpr bool bResetVersion = true;
		FMetasoundFrontendClassMetadata& ClassMetadata = Document.RootGraph.Metadata;
		InitGraphClassMetadata(ClassMetadata, bResetVersion);
	}

	// 2. Set default doc version Metadata
	{
		FMetasoundFrontendDocumentMetadata& DocMetadata = Document.Metadata;
		DocMetadata.Version.Number = FMetasoundFrontendDocument::GetMaxVersion();
	}

	// 3. Add default interfaces for given UClass
	{
		TArray<FMetasoundFrontendVersion> InitVersions = ISearchEngine::Get().FindUClassDefaultInterfaceVersions(GetBuilderClassPath());
		FModifyInterfaceOptions Options({ }, InitVersions);
		ModifyInterfaces(MoveTemp(Options));
	}
}

void FMetaSoundFrontendDocumentBuilder::InitNodeLocations()
{
#if WITH_EDITORONLY_DATA
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FMetasoundFrontendGraph& Graph = GetDocument().RootGraph.Graph;

	FVector2D InputNodeLocation = FVector2D::ZeroVector;
	FVector2D ExternalNodeLocation = InputNodeLocation + DisplayStyle::NodeLayout::DefaultOffsetX;
	FVector2D OutputNodeLocation = ExternalNodeLocation + DisplayStyle::NodeLayout::DefaultOffsetX;

	TArray<FMetasoundFrontendNode>& Nodes = Graph.Nodes;
	for (FMetasoundFrontendNode& Node : Nodes)
	{
		if (const int32* ClassIndex = DocumentCache->FindDependencyIndex(Node.ClassID))
		{
			FMetasoundFrontendClass& Class = GetDocument().Dependencies[*ClassIndex];

			const EMetasoundFrontendClassType NodeType = Class.Metadata.GetType();
			FVector2D NewLocation;
			if (NodeType == EMetasoundFrontendClassType::Input)
			{
				NewLocation = InputNodeLocation;
				InputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
			}
			else if (NodeType == EMetasoundFrontendClassType::Output)
			{
				NewLocation = OutputNodeLocation;
				OutputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
			}
			else
			{
				NewLocation = ExternalNodeLocation;
				ExternalNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
			}

			// TODO: Find consistent location for controlling node locations.
			// Currently it is split between MetasoundEditor and MetasoundFrontend modules.
			FMetasoundFrontendNodeStyle& Style = Node.Style;
			Style.Display.Locations = { { FGuid::NewGuid(), NewLocation } };
		}
	}
#endif // WITH_EDITORONLY_DATA
}

bool FMetaSoundFrontendDocumentBuilder::IsDependencyReferenced(const FGuid& InClassID) const
{
	return DocumentCache->GetNodeCache().ContainsNodesOfClassID(InClassID);
}

bool FMetaSoundFrontendDocumentBuilder::IsNodeInputConnected(const FGuid& InNodeID, const FGuid& InVertexID) const
{
	return DocumentCache->GetEdgeCache().IsNodeInputConnected(InNodeID, InVertexID);
}

bool FMetaSoundFrontendDocumentBuilder::IsNodeOutputConnected(const FGuid& InNodeID, const FGuid& InVertexID) const
{
	return DocumentCache->GetEdgeCache().IsNodeOutputConnected(InNodeID, InVertexID);
}

bool FMetaSoundFrontendDocumentBuilder::IsInterfaceDeclared(FName InInterfaceName) const
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendInterface Interface;
	if (ISearchEngine::Get().FindInterfaceWithHighestVersion(InInterfaceName, Interface))
	{
		return IsInterfaceDeclared(Interface.Version);
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::IsInterfaceDeclared(const FMetasoundFrontendVersion& InInterfaceVersion) const
{
	return GetDocument().Interfaces.Contains(InInterfaceVersion);
}

bool FMetaSoundFrontendDocumentBuilder::IsPreset() const
{
	return GetDocument().RootGraph.PresetOptions.bIsPreset;
}

bool FMetaSoundFrontendDocumentBuilder::ModifyInterfaces(Metasound::Frontend::FModifyInterfaceOptions&& InOptions)
{
	using namespace Metasound::Frontend;

	DocumentBuilderPrivate::FModifyInterfacesImpl Context(MoveTemp(InOptions));
	FMetasoundFrontendDocument& Doc = GetDocument();
	return Context.Execute(*this, Doc);
}

void FMetaSoundFrontendDocumentBuilder::ReloadCache()
{
	using namespace Metasound::Frontend;
	DocumentCache = MakeUnique<FDocumentCache>(GetDocument());
}

bool FMetaSoundFrontendDocumentBuilder::RemoveDependency(const FGuid& InClassID)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	if (const int32* IndexPtr = DocumentCache->FindDependencyIndex(InClassID))
	{
		TArray<FMetasoundFrontendClass>& Dependencies = Document.Dependencies;
		const int32 Index = *IndexPtr;

		TArray<const FMetasoundFrontendNode*> Nodes = NodeCache.FindNodesOfClassID(InClassID);
		for (const FMetasoundFrontendNode* Node : Nodes)
		{
			if (!RemoveNode(Node->GetID()))
			{
				return false;
			}
		}

		const int32 LastIndex = Dependencies.Num() - 1;
		DocumentCache->OnRemovingDependency(Index);
		if (Index != LastIndex)
		{
			DocumentCache->OnRemovingDependency(LastIndex);
		}
		Dependencies.RemoveAtSwap(Index, 1, false);
		if (Index != LastIndex)
		{
			DocumentCache->OnDependencyAdded(Index);
		}
	}

	return true;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveDependency(EMetasoundFrontendClassType ClassType, const FMetasoundFrontendClassName& InClassName, const FMetasoundFrontendVersionNumber& InClassVersionNumber)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	const FNodeRegistryKey ClassKey = NodeRegistryKey::CreateKey(ClassType, InClassName.GetFullName().ToString(), InClassVersionNumber.Major, InClassVersionNumber.Minor);
	if (const int32* IndexPtr = DocumentCache->FindDependencyIndex(ClassKey))
	{
		TArray<FMetasoundFrontendClass>& Dependencies = Document.Dependencies;
		const int32 Index = *IndexPtr;

		TArray<const FMetasoundFrontendNode*> Nodes = NodeCache.FindNodesOfClassID(Dependencies[Index].ID);
		for (const FMetasoundFrontendNode* Node : Nodes)
		{
			if (!RemoveNode(Node->GetID()))
			{
				return false;
			}
		}

		const int32 LastIndex = Dependencies.Num() - 1;
		DocumentCache->OnRemovingDependency(Index);
		if (Index != LastIndex)
		{
			DocumentCache->OnRemovingDependency(LastIndex);
		}
		Dependencies.RemoveAtSwap(Index, 1, false);
		if (Index != LastIndex)
		{
			DocumentCache->OnDependencyAdded(Index);
		}
	}

	return true;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveEdge(const FMetasoundFrontendEdge& EdgeToRemove)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
	TArray<FMetasoundFrontendEdge>& Edges = Graph.Edges;
	IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache();
	if (const int32* IndexPtr = EdgeCache.FindEdgeIndexToNodeInput(EdgeToRemove.ToNodeID, EdgeToRemove.ToVertexID))
	{
		const int32 Index = *IndexPtr;
		const int32 LastIndex = Edges.Num() - 1;
		EdgeCache.OnRemovingEdge(Index);
		if (Index != LastIndex)
		{
			EdgeCache.OnRemovingEdge(LastIndex);
		}
		Edges.RemoveAtSwap(Index, 1, false);
		if (Index != LastIndex)
		{
			EdgeCache.OnEdgeAdded(Index);
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveNamedEdges(const TSet<Metasound::Frontend::FNamedEdge>& InNamedEdgesToRemove, TArray<FMetasoundFrontendEdge>* OutRemovedEdges)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	if (OutRemovedEdges)
	{
		OutRemovedEdges->Reset();
	}

	bool bSuccess = true;

	TArray<FMetasoundFrontendEdge> EdgesToRemove;
	for (const FNamedEdge& NamedEdge : InNamedEdgesToRemove)
	{
		const FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
		const FMetasoundFrontendVertex* OutputVertex = NodeCache.FindOutputVertex(NamedEdge.OutputNodeID, NamedEdge.OutputName);
		const FMetasoundFrontendVertex* InputVertex = NodeCache.FindInputVertex(NamedEdge.InputNodeID, NamedEdge.InputName);

		if (OutputVertex && InputVertex)
		{
			FMetasoundFrontendEdge NewEdge = { NamedEdge.OutputNodeID, OutputVertex->VertexID, NamedEdge.InputNodeID, InputVertex->VertexID };
			if (ContainsEdge(NewEdge))
			{
				EdgesToRemove.Add(MoveTemp(NewEdge));
			}
			else
			{
				bSuccess = false;
				UE_LOG(LogMetaSound, Warning, TEXT("Failed to remove connection between MetaSound node output '%s' and input '%s': No connection found."), *NamedEdge.OutputName.ToString(), *NamedEdge.InputName.ToString());
			}
		}
	}

	for (FMetasoundFrontendEdge& EdgeToRemove : EdgesToRemove)
	{
		const bool bRemovedEdge = RemoveEdgeToNodeInput(EdgeToRemove.ToNodeID, EdgeToRemove.ToVertexID);
		if (ensureAlwaysMsgf(bRemovedEdge, TEXT("Failed to remove MetaSound graph edge via DocumentBuilder when prior step validated edge remove was valid")))
		{
			if (OutRemovedEdges)
			{
				OutRemovedEdges->Add(EdgeToRemove);
			}
		}
		else
		{
			bSuccess = false;
		}
	}

	return bSuccess;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveEdgesByNodeClassInterfaceBindings(const FGuid& InFromNodeID, const FGuid& InToNodeID)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	const FMetasoundFrontendDocument* FromNodeDocument = FindOrLoadNodeClassDocument(InFromNodeID);
	const FMetasoundFrontendDocument* ToNodeDocument = FindOrLoadNodeClassDocument(InToNodeID);
	if (FromNodeDocument && ToNodeDocument)
	{
		TSet<FNamedEdge> NamedEdges;
		if (DocumentBuilderPrivate::TryGetInterfaceBoundEdges(InFromNodeID, *FromNodeDocument, InToNodeID, *ToNodeDocument, NamedEdges))
		{
			return RemoveNamedEdges(NamedEdges);
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveEdgesFromNodeOutput(const FGuid& InNodeID, const FGuid& InVertexID)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
	IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache();

	if (const TArray<int32>* Indices = EdgeCache.FindEdgeIndicesFromNodeOutput(InNodeID, InVertexID))
	{
		TArray<int32> IndicesCopy = *Indices; // Copy off indices as the array may be modified when notifying the cache in the loop below
		for (int32 Index : IndicesCopy)
		{
			const int32 LastIndex = Graph.Edges.Num() - 1;
			EdgeCache.OnRemovingEdge(Index);
			if (Index != LastIndex)
			{
				EdgeCache.OnRemovingEdge(LastIndex);
			}
			Graph.Edges.RemoveAtSwap(Index, 1, false);
			if (Index != LastIndex)
			{
				EdgeCache.OnEdgeAdded(Index);
			}
		}

		return true;
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveEdgeToNodeInput(const FGuid& InNodeID, const FGuid& InVertexID)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	IDocumentGraphEdgeCache& EdgeCache = DocumentCache->GetEdgeCache();
	FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
	if (const int32* IndexPtr = EdgeCache.FindEdgeIndexToNodeInput(InNodeID, InVertexID))
	{
		const int32 Index = *IndexPtr; // Copy off indices as the pointer may be modified when notifying the cache below
		const int32 LastIndex = Graph.Edges.Num() - 1;
		EdgeCache.OnRemovingEdge(Index);
		if (Index != LastIndex)
		{
			EdgeCache.OnRemovingEdge(LastIndex);
		}
		Graph.Edges.RemoveAtSwap(Index, 1, false);
		if (Index != LastIndex)
		{
			EdgeCache.OnEdgeAdded(Index);
		}

		return true;
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveGraphInput(FName InInputName)
{
	if (const FMetasoundFrontendNode* Node = FindGraphInputNode(InInputName))
	{
		const FGuid ClassID = Node->ClassID;
		if (RemoveNode(Node->GetID()))
		{
			TArray<FMetasoundFrontendClassInput>& Inputs = GetDocument().RootGraph.Interface.Inputs;
			auto InputNameMatches = [InInputName](const FMetasoundFrontendClassInput& Input) { return Input.Name == InInputName; };
			if (Inputs.RemoveAllSwap(InputNameMatches))
			{
				if (IsDependencyReferenced(ClassID))
				{
					return true;
				}
				else
				{
					if (RemoveDependency(ClassID))
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveGraphOutput(FName InOutputName)
{
	if (const FMetasoundFrontendNode* Node = FindGraphOutputNode(InOutputName))
	{
		const FGuid ClassID = Node->ClassID;
		if (RemoveNode(Node->GetID()))
		{
			TArray<FMetasoundFrontendClassOutput>& Outputs = GetDocument().RootGraph.Interface.Outputs;
			auto OutputNameMatches = [InOutputName](const FMetasoundFrontendClassOutput& Output) { return Output.Name == InOutputName; };
			if (Outputs.RemoveAllSwap(OutputNameMatches))
			{
				if (IsDependencyReferenced(ClassID))
				{
					return true;
				}
				else
				{
					if (RemoveDependency(ClassID))
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveInterface(FName InterfaceName)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendInterface Interface;
	if (ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceName, Interface))
	{
		if (!GetDocument().Interfaces.Contains(Interface.Version))
		{
			UE_LOG(LogMetaSound, VeryVerbose, TEXT("MetaSound interface '%s' not found on document. MetaSoundBuilder skipping remove request."), *InterfaceName.ToString());
			return true;
		}

		const FTopLevelAssetPath BuilderClassPath = GetBuilderClassPath();
		const FInterfaceRegistryKey Key = GetInterfaceRegistryKey(Interface.Version);
		if (const IInterfaceRegistryEntry* Entry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(Key))
		{
			const FMetasoundFrontendInterfaceUClassOptions* ClassOptions = Entry->GetInterface().FindClassOptions(BuilderClassPath);
			if (ClassOptions && !ClassOptions->bIsModifiable)
			{
				UE_LOG(LogMetaSound, Error, TEXT("DocumentBuilder failed to remove MetaSound Interface '%s' to document: is not set to be modifiable for given UClass '%s'"), *InterfaceName.ToString(), *BuilderClassPath.ToString());
				return false;
			}
		}

		const FModifyRootGraphInterfaces ModifyInterfaces({ { Interface.Version }, { } });
		if (ModifyInterfaces.Transform(GetDocument()))
		{
			// TODO: Add ability to update local builder caches dynamically
			// in ModifyRootGraphInterfaces transform once it moves away
			// from using controllers internally. Then cache updates can
			// be more isolated and not use this full rebuild call.
			ReloadCache();
			return true;
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::RemoveNode(const FGuid& InNodeID)
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::RemoveNode);

	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
	TArray<FMetasoundFrontendNode>& Nodes = Graph.Nodes;
	if (const int32* IndexPtr = NodeCache.FindNodeIndex(InNodeID))
	{
		const int32 Index = *IndexPtr; // Copy off indices as the pointer may be modified when notifying the cache below
		const FMetasoundFrontendNode& Node = Nodes[Index];

		for (const FMetasoundFrontendVertex& Vertex : Node.Interface.Inputs)
		{
			RemoveEdgeToNodeInput(InNodeID, Vertex.VertexID);
		}

		for (const FMetasoundFrontendVertex& Vertex : Node.Interface.Outputs)
		{
			RemoveEdgesFromNodeOutput(InNodeID, Vertex.VertexID);
		}

		NodeCache.OnRemovingNode(Index);

		const int32 LastIndex = Nodes.Num() - 1;
		if (Index != LastIndex)
		{
			NodeCache.OnRemovingNode(LastIndex);
		}
		Nodes.RemoveAtSwap(Index, 1, false);
		if (Index != LastIndex)
		{
			NodeCache.OnNodeAdded(Index);
		}

#if WITH_EDITORONLY_DATA
		Document.Metadata.ModifyContext.AddNodeIDModified(InNodeID);
#endif // WITH_EDITORONLY_DATA

		return true;
	}

	return false;
}

#if WITH_EDITOR
void FMetaSoundFrontendDocumentBuilder::SetAuthor(const FString& InAuthor)
{
	FMetasoundFrontendClassMetadata& ClassMetadata = GetDocument().RootGraph.Metadata;
	ClassMetadata.SetAuthor(InAuthor);
}
#endif // WITH_EDITOR

bool FMetaSoundFrontendDocumentBuilder::SetGraphInputDefault(FName InputName, const FMetasoundFrontendLiteral& InDefaultLiteral)
{
	using namespace Metasound::Frontend;

	auto NameMatchesInput = [&InputName](const FMetasoundFrontendClassInput& Input) { return Input.Name == InputName; };
	TArray<FMetasoundFrontendClassInput>& Inputs = GetDocument().RootGraph.Interface.Inputs;
	if (FMetasoundFrontendClassInput* Input = Inputs.FindByPredicate(NameMatchesInput))
	{
		if (IDataTypeRegistry::Get().IsLiteralTypeSupported(Input->TypeName, InDefaultLiteral.GetType()))
		{
			Input->DefaultLiteral = InDefaultLiteral;

			// Set the input as no longer inheriting default for presets
			if (IsPreset())
			{
				return SetInputInheritsDefault(InputName, /*bInputInheritsDefault=*/false);
			}
			return true;
		}
		UE_LOG(LogMetaSound, Error, TEXT("Attempting to set graph input of type '%s' with unsupported literal type"), *Input->TypeName.ToString());
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::SetNodeInputDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FMetasoundFrontendLiteral& InLiteral)
{
	using namespace Metasound::Frontend;

	if (const int32* Index = DocumentCache->GetNodeCache().FindNodeIndex(InNodeID))
	{
		FMetasoundFrontendGraph& Graph = GetDocument().RootGraph.Graph;
		FMetasoundFrontendNode& Node = Graph.Nodes[*Index];
		TArray<FMetasoundFrontendVertex>& Inputs = Node.Interface.Inputs;

		auto IsVertex = [&InVertexID](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == InVertexID; };
		if (Inputs.ContainsByPredicate(IsVertex))
		{
			FMetasoundFrontendVertexLiteral NewVertexLiteral;
			NewVertexLiteral.VertexID = InVertexID;
			NewVertexLiteral.Value = InLiteral;

			auto IsLiteral = [&InVertexID](const FMetasoundFrontendVertexLiteral& Literal) { return Literal.VertexID == InVertexID; };
			if (FMetasoundFrontendVertexLiteral* InputLiteral = Node.InputLiterals.FindByPredicate(IsLiteral))
			{
				*InputLiteral = MoveTemp(NewVertexLiteral);
			}
			else
			{
				Node.InputLiterals.Add(MoveTemp(NewVertexLiteral));
			}

			return true;
		}
	}

	return false;
}


bool FMetaSoundFrontendDocumentBuilder::SetInputInheritsDefault(FName InName, bool bInputInheritsDefault)
{
	FMetasoundFrontendGraphClassPresetOptions& PresetOptions = GetDocument().RootGraph.PresetOptions;
	if (bInputInheritsDefault)
	{
		if (PresetOptions.bIsPreset)
		{
			return PresetOptions.InputsInheritingDefault.Add(InName).IsValidId();
		}
	}
	else
	{
		if (PresetOptions.bIsPreset)
		{
			return PresetOptions.InputsInheritingDefault.Remove(InName) > 0;
		}
	}

	return false;
}

bool FMetaSoundFrontendDocumentBuilder::SwapGraphInput(const FMetasoundFrontendClassVertex& InExistingInputVertex, const FMetasoundFrontendClassVertex& InNewInputVertex)
{
	using namespace Metasound::Frontend;

	// 1. Check if equivalent and early out if functionally do not match
	{
		const FMetasoundFrontendClassInput* ClassInput = FindGraphInput(InExistingInputVertex.Name);
		if (!ClassInput || !FMetasoundFrontendVertex::IsFunctionalEquivalent(*ClassInput, InExistingInputVertex))
		{
			return false;
		}
	}

	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	// 2. Gather data from existing member/node needed to swap
	TArray<FMetasoundFrontendEdge> RemovedEdges;

	const FMetasoundFrontendNode* ExistingInputNode = NodeCache.FindInputNode(InExistingInputVertex.Name);
	checkf(ExistingInputNode, TEXT("'SwapGraphInput' failed to find original input node"));
	const FGuid NodeID = ExistingInputNode->GetID();

#if WITH_EDITOR
	TMap<FGuid, FVector2D> Locations;
#endif // WITH_EDITOR
	{
#if WITH_EDITOR
		Locations = ExistingInputNode->Style.Display.Locations;
#endif // WITH_EDITOR

		const FGuid VertexID = ExistingInputNode->Interface.Outputs.Last().VertexID;
		TArray<const FMetasoundFrontendEdge*> Edges = DocumentCache->GetEdgeCache().FindEdges(ExistingInputNode->GetID(), VertexID);
		Algo::Transform(Edges, RemovedEdges, [](const FMetasoundFrontendEdge* Edge) { return *Edge; });
	}

	// 3. Remove existing graph vertex
	{
		const bool bRemovedVertex = RemoveGraphOutput(InExistingInputVertex.Name);
		checkf(bRemovedVertex, TEXT("Failed to swap MetaSound input expected to exist"));
	}

	// 4. Add new graph vertex
	FMetasoundFrontendClassInput NewInput = InNewInputVertex;
	NewInput.NodeID = NodeID;
#if WITH_EDITOR
	NewInput.Metadata.SetSerializeText(InExistingInputVertex.Metadata.GetSerializeText());
#endif // WITH_EDITOR

	const FMetasoundFrontendNode* NewInputNode = AddGraphInput(NewInput);
	checkf(NewInputNode, TEXT("Failed to add new Input node when swapping graph inputs"));

#if WITH_EDITOR
	// 5a. Add to new copy existing node locations
	if (!Locations.IsEmpty())
	{
		const int32* NodeIndex = NodeCache.FindNodeIndex(NewInputNode->GetID());
		checkf(NodeIndex, TEXT("Cache was not updated to reflect newly added input node"));
		FMetasoundFrontendNode& NewNode = GetDocument().RootGraph.Graph.Nodes[*NodeIndex];
		NewNode.Style.Display.Locations = Locations;
	}
#endif // WITH_EDITOR

	// 5b. Add to new copy existing node edges
	for (const FMetasoundFrontendEdge& RemovedEdge : RemovedEdges)
	{
		FMetasoundFrontendEdge NewEdge = RemovedEdge;
		NewEdge.FromNodeID = NewInputNode->GetID();
		NewEdge.FromVertexID = NewInputNode->Interface.Outputs.Last().VertexID;
		const bool bEdgeAdded = AddEdge(MoveTemp(NewEdge)) != nullptr;
		checkf(bEdgeAdded, TEXT("Failed to add replacement edge when swapping paired inputs"));
	}
	return true;
}

bool FMetaSoundFrontendDocumentBuilder::SwapGraphOutput(const FMetasoundFrontendClassVertex& InExistingOutputVertex, const FMetasoundFrontendClassVertex& InNewOutputVertex)
{
	using namespace Metasound::Frontend;

	// 1. Check if equivalent and early out if functionally do not match
	{
		const FMetasoundFrontendClassOutput* ClassOutput = FindGraphOutput(InExistingOutputVertex.Name);
		if (!ClassOutput || !FMetasoundFrontendVertex::IsFunctionalEquivalent(*ClassOutput, InExistingOutputVertex))
		{
			return false;
		}
	}

	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
	const FMetasoundFrontendNode* ExistingOutputNode = NodeCache.FindOutputNode(InExistingOutputVertex.Name);
	checkf(ExistingOutputNode, TEXT("'SwapGraphOutput' failed to find original output node"));
	const FGuid NodeID = ExistingOutputNode->GetID();

	// 2. Gather data from existing member/node needed to swap
	TArray<FMetasoundFrontendEdge> RemovedEdges;
#if WITH_EDITOR
	TMap<FGuid, FVector2D> Locations;
#endif // WITH_EDITOR
	{
#if WITH_EDITOR
		Locations = ExistingOutputNode->Style.Display.Locations;
#endif // WITH_EDITOR

		const FGuid VertexID = ExistingOutputNode->Interface.Inputs.Last().VertexID;
		TArray<const FMetasoundFrontendEdge*> Edges = DocumentCache->GetEdgeCache().FindEdges(ExistingOutputNode->GetID(), VertexID);
		Algo::Transform(Edges, RemovedEdges, [](const FMetasoundFrontendEdge* Edge) { return *Edge; });
	}

	// 3. Remove existing graph vertex
	{
		const bool bRemovedVertex = RemoveGraphOutput(InExistingOutputVertex.Name);
		checkf(bRemovedVertex, TEXT("Failed to swap output expected to exist while swapping MetaSound outputs"));
	}
	
	// 4. Add new graph vertex
	FMetasoundFrontendClassOutput NewOutput = InNewOutputVertex;
	NewOutput.NodeID = NodeID;
#if WITH_EDITOR
	NewOutput.Metadata.SetSerializeText(InExistingOutputVertex.Metadata.GetSerializeText());
#endif // WITH_EDITOR

	const FMetasoundFrontendNode* NewOutputNode = AddGraphOutput(NewOutput);
	checkf(NewOutputNode, TEXT("Failed to add new output node when swapping graph outputs"));

#if WITH_EDITOR
	// 5a. Add to new copy existing node locations
	if (!Locations.IsEmpty())
	{
		const int32* NodeIndex = NodeCache.FindNodeIndex(NewOutputNode->GetID());
		checkf(NodeIndex, TEXT("Cache was not updated to reflect newly added input node"));
		FMetasoundFrontendNode& NewNode = GetDocument().RootGraph.Graph.Nodes[*NodeIndex];
		NewNode.Style.Display.Locations = Locations;
	}
#endif // WITH_EDITOR

	// 5b. Add to new copy existing node edges
	for (const FMetasoundFrontendEdge& RemovedEdge : RemovedEdges)
	{
		FMetasoundFrontendEdge NewEdge = RemovedEdge;
		NewEdge.ToNodeID = NewOutputNode->GetID();
		NewEdge.ToVertexID = NewOutputNode->Interface.Inputs.Last().VertexID;
		const bool bEdgeAdded = AddEdge(MoveTemp(NewEdge)) != nullptr;
		checkf(bEdgeAdded, TEXT("Failed to add replacement edge when swapping paired outputs"));
	}

	return true;
}

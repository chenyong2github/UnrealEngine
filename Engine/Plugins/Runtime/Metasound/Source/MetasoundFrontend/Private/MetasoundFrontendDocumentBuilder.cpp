// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFrontendDocumentBuilder.h"

#include "Algo/AnyOf.h"
#include "Algo/Find.h"
#include "Algo/NoneOf.h"
#include "Algo/Sort.h"
#include "Algo/Transform.h"
#include "Interfaces/MetasoundFrontendInterfaceBindingRegistry.h"
#include "Interfaces/MetasoundFrontendInterfaceRegistry.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAssetManager.h"
#include "MetasoundDocumentInterface.h"
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
				UE_LOG(LogMetaSound, Error, TEXT("Node associated with graph vertex of type '%s' does not contain input vertex with type '%s'"), *InVertex.TypeName.ToString());
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
	} // namespace DocumentBuilderPrivate
} // namespace Metasound::Frontend

FMetaSoundFrontendDocumentBuilder::FMetaSoundFrontendDocumentBuilder()
{
}

FMetaSoundFrontendDocumentBuilder::FMetaSoundFrontendDocumentBuilder(TScriptInterface<IMetaSoundDocumentInterface> InDocumentInterface)
	: DocumentInterface(InDocumentInterface)
{
	ReloadCache();
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

	FMetasoundFrontendGraphClass& RootGraph = GetDocument().RootGraph;

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

	FMetasoundFrontendGraphClass& RootGraph = GetDocument().RootGraph;

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
			return true;
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

	FMetasoundFrontendDocument& Document = GetDocument();
	FMetasoundFrontendGraphClass& RootGraphClass = Document.RootGraph;
	FMetasoundFrontendGraphClassPresetOptions& PresetOptions = RootGraphClass.PresetOptions;
	PresetOptions.bIsPreset = false;

#if WITH_EDITOR
	FMetasoundFrontendGraphStyle Style = Document.RootGraph.Graph.Style;
	Style.bIsGraphEditable = true;
#endif // WITH_EDITOR

	return true;
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

const FMetasoundFrontendNode* FMetaSoundFrontendDocumentBuilder::FindGraphInputNode(FName InputName) const
{
	using namespace Metasound::Frontend;
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();
	return NodeCache.FindInputNode(InputName);
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

FMetasoundFrontendDocument& FMetaSoundFrontendDocumentBuilder::GetDocument()
{
	IMetaSoundDocumentInterface* Interface = DocumentInterface.GetInterface();
	return Interface->GetDocument();
}

const FMetasoundFrontendDocument& FMetaSoundFrontendDocumentBuilder::GetDocument() const
{
	return DocumentInterface->GetDocument();
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

void FMetaSoundFrontendDocumentBuilder::InitDocument(FName UClassName)
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundFrontendDocumentBuilder::InitDocument);

	using namespace Metasound::Frontend;

	// 1. Set default class Metadata
	{
		constexpr bool bResetVersion = true;
		FMetasoundFrontendClassMetadata& ClassMetadata = GetDocument().RootGraph.Metadata;
		InitGraphClassMetadata(ClassMetadata, bResetVersion);
	}

	// 2. Set default doc version Metadata
	{
		FMetasoundFrontendDocumentMetadata& DocMetadata = GetDocument().Metadata;
		DocMetadata.Version.Number = FMetasoundFrontendDocument::GetMaxVersion();
	}

	// 3. Add default interfaces for given UClass
	{
		TArray<FMetasoundFrontendInterface> InitInterfaces = ISearchEngine::Get().FindUClassDefaultInterfaces(UClassName);
		FModifyRootGraphInterfaces ModifyRootGraphInterfaces({ }, InitInterfaces);
		ModifyRootGraphInterfaces.Transform(GetDocument());
	}

	// 5. Reload the whole cache as transforms above mutate cached collections. Can be safely removed once transforms above
	// are re-implemented in builder and support cache to enable marginally better performance on document initialization.
	ReloadCache();
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

void FMetaSoundFrontendDocumentBuilder::ReloadCache()
{
	using namespace Metasound::Frontend;
	DocumentCache = MakeUnique<FDocumentCache>(GetDocument());
}

bool FMetaSoundFrontendDocumentBuilder::RemoveDependency(EMetasoundFrontendClassType ClassType, const FMetasoundFrontendClassName& InClassName, const FMetasoundFrontendVersionNumber& InClassVersionNumber)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendDocument& Document = GetDocument();
	const IDocumentGraphNodeCache& NodeCache = DocumentCache->GetNodeCache();

	TArray<FMetasoundFrontendClass>& Dependencies = Document.Dependencies;
	const FNodeRegistryKey ClassKey = NodeRegistryKey::CreateKey(ClassType, InClassName.GetFullName().ToString(), InClassVersionNumber.Major, InClassVersionNumber.Minor);
	if (const int32* IndexPtr = DocumentCache->FindDependencyIndex(ClassKey))
	{
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

bool FMetaSoundFrontendDocumentBuilder::RemoveInterface(FName InterfaceName)
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendInterface Interface;
	if (ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceName, Interface))
	{
		if (!GetDocument().Interfaces.Contains(Interface.Version))
		{
			return true;
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
			if (!RemoveEdgeToNodeInput(Node.GetID(), Vertex.VertexID))
			{
				return false;
			}
		}

		for (const FMetasoundFrontendVertex& Vertex : Node.Interface.Outputs)
		{
			RemoveEdgesFromNodeOutput(Node.GetID(), Vertex.VertexID);
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

bool FMetaSoundFrontendDocumentBuilder::ModifyInterfaces(TArray<FMetasoundFrontendVersion> OutputFormatsToRemove, TArray<FMetasoundFrontendVersion> OutputFormatsToAdd)
{
	using namespace Metasound::Frontend;

	// Integrating this transform into the builder will allow for more selective updates and not having to rebuild the cache from scratch after performing the transform.
	FModifyRootGraphInterfaces ModifyRootGraph(OutputFormatsToRemove, OutputFormatsToAdd);
	if (ModifyRootGraph.Transform(GetDocument()))
	{
		ReloadCache();
		return true;
	}

	return false;
}

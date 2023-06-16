// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundBuilderSubsystem.h"

#include "Algo/Find.h"
#include "Algo/Transform.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/MetasoundOutputFormatInterfaces.h"
#include "Interfaces/MetasoundFrontendSourceInterface.h"
#include "Metasound.h"
#include "MetasoundAssetSubsystem.h"
#include "MetasoundDataReference.h"
#include "MetasoundDynamicOperatorTransactor.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundGeneratorHandle.h"
#include "MetasoundLog.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundSource.h"
#include "MetasoundTrace.h"
#include "MetasoundUObjectRegistry.h"
#include "MetasoundVertex.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetasoundBuilderSubsystem)


namespace Metasound::Engine
{
	namespace BuilderSubsystemPrivate
	{
		static int32 LiveUpdateEnabledCVar = 0;

		template <typename TLiteralType>
		FMetasoundFrontendLiteral CreatePODMetaSoundLiteral(const TLiteralType& Value, FName& OutDataType)
		{
			OutDataType = GetMetasoundDataTypeName<TLiteralType>();

			FMetasoundFrontendLiteral Literal;
			Literal.Set(Value);
			return Literal;
		}

		TUniquePtr<INode> CreateDynamicNodeFromFrontendLiteral(const FName DataType, const FMetasoundFrontendLiteral& InLiteral)
		{
			using namespace Frontend;
			FLiteral ValueLiteral = InLiteral.ToLiteral(DataType);

			// TODO: Node name "Literal" is always the same.  Consolidate and deprecate providing unique node name to avoid unnecessary FName table bloat.
			FLiteralNodeConstructorParams Params { "Literal", FGuid::NewGuid(), MoveTemp(ValueLiteral) };
			return IDataTypeRegistry::Get().CreateLiteralNode(DataType, MoveTemp(Params));
		}

		template <typename BuilderClass>
		BuilderClass& CreateTransientBuilder()
		{
			const EObjectFlags NewObjectFlags = RF_Public | RF_Transient;
			UPackage* TransientPackage = GetTransientPackage();
			const FName ObjectName = MakeUniqueObjectName(TransientPackage, BuilderClass::StaticClass());
			TObjectPtr<BuilderClass> NewBuilder = NewObject<BuilderClass>(TransientPackage, ObjectName, NewObjectFlags);
			check(NewBuilder);
			NewBuilder->InitFrontendBuilder();
			return *NewBuilder.Get();
		}
	} // namespace BuilderSubsystemPrivate
} // namespace Metasound::Engine

FAutoConsoleVariableRef CVarMetaSoundEditorAudioConnectSpacing(
	TEXT("au.MetaSound.Builder.Audition.LiveUpdatesEnabled"),
	Metasound::Engine::BuilderSubsystemPrivate::LiveUpdateEnabledCVar,
	TEXT("Whether or not the MetaSound builder's audition feature supports live updates (mutating a MetaSound graph while it is being executed at runtime).\n")
	TEXT("Values: 0 (Disabled), !=0 (Enabled)"),
	ECVF_Default);


FMetaSoundBuilderNodeOutputHandle UMetaSoundBuilderBase::AddGraphInputNode(FName Name, FName DataType, FMetasoundFrontendLiteral DefaultValue, EMetaSoundBuilderResult& OutResult, bool bIsConstructorInput)
{
	FMetaSoundBuilderNodeOutputHandle NewHandle;

	const FMetasoundFrontendNode* Node = Builder.FindGraphInputNode(Name);
	if (Node)
	{
		UE_LOG(LogMetaSound, Warning, TEXT("AddGraphInputNode Failed: Input Node already exists with name '%s'; returning handle to existing node which may or may not match requested DataType '%s'"), *Name.ToString(), *DataType.ToString());
	}
	else
	{
		FMetasoundFrontendClassInput Description;
		Description.Name = Name;
		Description.TypeName = DataType;
		Description.NodeID = FGuid::NewGuid();
		Description.VertexID = FGuid::NewGuid();
		Description.DefaultLiteral = static_cast<FMetasoundFrontendLiteral>(DefaultValue);
		Description.AccessType = bIsConstructorInput ? EMetasoundFrontendVertexAccessType::Value : EMetasoundFrontendVertexAccessType::Reference;
		Node = Builder.AddGraphInput(Description);
	}

	if (Node)
	{
		const TArray<FMetasoundFrontendVertex>& Outputs = Node->Interface.Outputs;
		checkf(!Outputs.IsEmpty(), TEXT("Node should be initialized and have one output."));

		NewHandle.NodeID = Node->GetID();
		NewHandle.VertexID = Outputs.Last().VertexID;
	}

	OutResult = NewHandle.IsSet() ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
	return NewHandle;
}

FMetaSoundBuilderNodeInputHandle UMetaSoundBuilderBase::AddGraphOutputNode(FName Name, FName DataType, FMetasoundFrontendLiteral DefaultValue, EMetaSoundBuilderResult& OutResult, bool bIsConstructorOutput)
{
	FMetaSoundBuilderNodeInputHandle NewHandle;
	const FMetasoundFrontendNode* Node = Builder.FindGraphOutputNode(Name);
	if (Node)
	{
		UE_LOG(LogMetaSound, Warning, TEXT("AddGraphOutputNode Failed: Output Node already exists with name '%s'; returning handle to existing node which may or may not match requested DataType '%s'"), *Name.ToString(), *DataType.ToString());
	}
	else
	{
		FMetasoundFrontendClassOutput Description;
		Description.Name = Name;
		Description.TypeName = DataType;
		Description.NodeID = FGuid::NewGuid();
		Description.VertexID = FGuid::NewGuid();
		Description.AccessType = bIsConstructorOutput ? EMetasoundFrontendVertexAccessType::Value : EMetasoundFrontendVertexAccessType::Reference;
		Node = Builder.AddGraphOutput(Description);
	}

	if (Node)
	{
		const TArray<FMetasoundFrontendVertex>& Inputs = Node->Interface.Inputs;
		checkf(!Inputs.IsEmpty(), TEXT("Node should be initialized and have one input."));

		const FGuid& VertexID = Inputs.Last().VertexID;
		if (Builder.SetNodeInputDefault(Node->GetID(), VertexID, DefaultValue))
		{
			NewHandle.NodeID = Node->GetID();
			NewHandle.VertexID = VertexID;
		}
	}

	OutResult = NewHandle.IsSet() ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
	return NewHandle;
}

void UMetaSoundBuilderBase::AddInterface(FName InterfaceName, EMetaSoundBuilderResult& OutResult)
{
	const bool bInterfaceAdded = Builder.AddInterface(InterfaceName);
	OutResult = bInterfaceAdded ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

FMetaSoundNodeHandle UMetaSoundBuilderBase::AddNode(const TScriptInterface<IMetaSoundDocumentInterface>& NodeClass, EMetaSoundBuilderResult& OutResult)
{
	using namespace Metasound;

	FMetaSoundNodeHandle NewHandle;

	if (NodeClass)
	{
		if (FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(NodeClass.GetObject()))
		{
			MetaSoundAsset->RegisterGraphWithFrontend();

			const IMetaSoundDocumentInterface* Interface = NodeClass.GetInterface();
			check(Interface);
			const FMetasoundFrontendDocument& NodeClassDoc = Interface->GetDocument();
			const FMetasoundFrontendGraphClass& NodeClassGraph = NodeClassDoc.RootGraph;

			if (const FMetasoundFrontendNode* NewNode = Builder.AddGraphNode(NodeClassGraph))
			{
				NewHandle.NodeID = NewNode->GetID();
			}
		}
	}

	OutResult = NewHandle.IsSet() ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
	return NewHandle;
}

FMetaSoundNodeHandle UMetaSoundBuilderBase::AddNodeByClassName(const FMetasoundFrontendClassName& ClassName, int32 MajorVersion, EMetaSoundBuilderResult& OutResult)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FMetaSoundNodeHandle NewHandle;
	if (const FMetasoundFrontendNode* NewNode = Builder.AddNodeByClassName(ClassName, MajorVersion))
	{
		NewHandle.NodeID = NewNode->GetID();
	}

	OutResult = NewHandle.IsSet() ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
	return NewHandle;
}

bool UMetaSoundBuilderBase::ContainsNode(const FMetaSoundNodeHandle& NodeHandle) const
{
	return Builder.ContainsNode(NodeHandle.NodeID);
}

bool UMetaSoundBuilderBase::ContainsNodeInput(const FMetaSoundBuilderNodeInputHandle& InputHandle) const
{
	return Builder.FindNodeInput(InputHandle.NodeID, InputHandle.VertexID) != nullptr;
}

bool UMetaSoundBuilderBase::ContainsNodeOutput(const FMetaSoundBuilderNodeOutputHandle& OutputHandle) const
{
	return Builder.FindNodeOutput(OutputHandle.NodeID, OutputHandle.VertexID) != nullptr;
}

void UMetaSoundBuilderBase::ConnectNodes(const FMetaSoundBuilderNodeOutputHandle& NodeOutputHandle, const FMetaSoundBuilderNodeInputHandle& NodeInputHandle, EMetaSoundBuilderResult& OutResult)
{
	const FMetasoundFrontendEdge* NewEdge = Builder.AddEdge(FMetasoundFrontendEdge
	{
		NodeOutputHandle.NodeID,
		NodeOutputHandle.VertexID,
		NodeInputHandle.NodeID,
		NodeInputHandle.VertexID,
	});
	OutResult = NewEdge ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

void UMetaSoundBuilderBase::ConnectNodesByInterfaceBindings(const FMetaSoundNodeHandle& FromNodeHandle, const FMetaSoundNodeHandle& ToNodeHandle, EMetaSoundBuilderResult& OutResult)
{
	const bool bEdgesAdded = Builder.AddEdgesByNodeClassInterfaceBindings(FromNodeHandle.NodeID, ToNodeHandle.NodeID);
	OutResult = bEdgesAdded ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

TArray<FMetaSoundBuilderNodeInputHandle> UMetaSoundBuilderBase::ConnectNodeOutputsToMatchingGraphInterfaceOutputs(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult)
{
	TArray<const FMetasoundFrontendEdge*> NewEdges;
	const bool bEdgesAdded = Builder.AddEdgesFromMatchingInterfaceNodeOutputsToGraphOutputs(NodeHandle.NodeID, NewEdges);
	OutResult = bEdgesAdded ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;

	TArray<FMetaSoundBuilderNodeInputHandle> ConnectedVertices;
	Algo::Transform(NewEdges, ConnectedVertices, [this](const FMetasoundFrontendEdge* NewEdge)
	{
		const FMetasoundFrontendVertex* Vertex = Builder.FindNodeInput(NewEdge->ToNodeID, NewEdge->ToVertexID);
		checkf(Vertex, TEXT("Edge connection reported success but vertex not found."));
		return FMetaSoundBuilderNodeInputHandle(NewEdge->ToNodeID, Vertex->VertexID);
	});

	return ConnectedVertices;
}

TArray<FMetaSoundBuilderNodeOutputHandle> UMetaSoundBuilderBase::ConnectNodeInputsToMatchingGraphInterfaceInputs(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult)
{
	TArray<const FMetasoundFrontendEdge*> NewEdges;
	const bool bEdgesAdded = Builder.AddEdgesFromMatchingInterfaceNodeInputsToGraphInputs(NodeHandle.NodeID, NewEdges);
	OutResult = bEdgesAdded ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;

	TArray<FMetaSoundBuilderNodeOutputHandle> ConnectedVertices;
	Algo::Transform(NewEdges, ConnectedVertices, [this](const FMetasoundFrontendEdge* NewEdge)
	{
		const FMetasoundFrontendVertex* Vertex = Builder.FindNodeInput(NewEdge->FromNodeID, NewEdge->FromVertexID);
		checkf(Vertex, TEXT("Edge connection reported success but vertex not found."));
		return FMetaSoundBuilderNodeOutputHandle(NewEdge->ToNodeID, Vertex->VertexID);
	});

	return ConnectedVertices;
}

void UMetaSoundBuilderBase::ConnectNodeOutputToGraphOutput(FName GraphOutputName, const FMetaSoundBuilderNodeOutputHandle& NodeOutputHandle, EMetaSoundBuilderResult& OutResult)
{
	OutResult = EMetaSoundBuilderResult::Failed;

	if (const FMetasoundFrontendNode* GraphOutputNode = Builder.FindGraphOutputNode(GraphOutputName))
	{
		const FMetasoundFrontendVertex& InputVertex = GraphOutputNode->Interface.Inputs.Last();
		const FMetasoundFrontendEdge* EdgeAdded = Builder.AddEdge(FMetasoundFrontendEdge
		{
			NodeOutputHandle.NodeID,
			NodeOutputHandle.VertexID,
			GraphOutputNode->GetID(),
			InputVertex.VertexID
		});
		OutResult = EdgeAdded ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
	}
}

void UMetaSoundBuilderBase::ConnectNodeInputToGraphInput(FName GraphInputName, const FMetaSoundBuilderNodeInputHandle& NodeInputHandle, EMetaSoundBuilderResult& OutResult)
{
	const FMetasoundFrontendNode* GraphInputNode = Builder.FindGraphInputNode(GraphInputName);
	if (!GraphInputNode)
	{
		OutResult = EMetaSoundBuilderResult::Failed;
		return;
	}

	const FMetasoundFrontendVertex& OutputVertex = GraphInputNode->Interface.Outputs.Last();
	const FMetasoundFrontendEdge* EdgeAdded = Builder.AddEdge(FMetasoundFrontendEdge
	{
		GraphInputNode->GetID(),
		OutputVertex.VertexID,
		NodeInputHandle.NodeID,
		NodeInputHandle.VertexID
	});
	OutResult = EdgeAdded ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

void UMetaSoundBuilderBase::DisconnectNodes(const FMetaSoundBuilderNodeOutputHandle& NodeOutputHandle, const FMetaSoundBuilderNodeInputHandle& NodeInputHandle, EMetaSoundBuilderResult& OutResult)
{
	const bool bEdgeRemoved = Builder.RemoveEdge(FMetasoundFrontendEdge
	{
		NodeOutputHandle.NodeID,
		NodeOutputHandle.VertexID,
		NodeInputHandle.NodeID,
		NodeInputHandle.VertexID,
	});
	OutResult = bEdgeRemoved ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

void UMetaSoundBuilderBase::DisconnectNodeInput(const FMetaSoundBuilderNodeInputHandle& NodeInputHandle, EMetaSoundBuilderResult& OutResult)
{
	const bool bEdgeRemoved = Builder.RemoveEdgeToNodeInput(NodeInputHandle.NodeID, NodeInputHandle.VertexID);
	OutResult = bEdgeRemoved ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

void UMetaSoundBuilderBase::DisconnectNodeOutput(const FMetaSoundBuilderNodeOutputHandle& NodeOutputHandle, EMetaSoundBuilderResult& OutResult)
{
	const bool bEdgeRemoved = Builder.RemoveEdgesFromNodeOutput(NodeOutputHandle.NodeID, NodeOutputHandle.VertexID);
	OutResult = bEdgeRemoved ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

void UMetaSoundBuilderBase::DisconnectNodesByInterfaceBindings(const FMetaSoundNodeHandle& FromNodeHandle, const FMetaSoundNodeHandle& ToNodeHandle, EMetaSoundBuilderResult& OutResult)
{
	const bool bEdgesRemoved = Builder.RemoveEdgesByNodeClassInterfaceBindings(FromNodeHandle.NodeID, ToNodeHandle.NodeID);
	OutResult = bEdgesRemoved ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

FMetaSoundBuilderNodeInputHandle UMetaSoundBuilderBase::FindNodeInputByName(const FMetaSoundNodeHandle& NodeHandle, FName InputName, EMetaSoundBuilderResult& OutResult)
{
	if (const FMetasoundFrontendNode* Node = Builder.FindNode(NodeHandle.NodeID))
	{
		const TArray<FMetasoundFrontendVertex>& InputVertices = Node->Interface.Inputs;

		auto FindByNamePredicate = [&InputName](const FMetasoundFrontendVertex& Vertex) { return Vertex.Name == InputName; };
		if (const FMetasoundFrontendVertex* Input = InputVertices.FindByPredicate(FindByNamePredicate))
		{
			OutResult = EMetaSoundBuilderResult::Succeeded;
			return FMetaSoundBuilderNodeInputHandle(Node->GetID(), Input->VertexID);
		}

		FString NodeClassName = TEXT("N/A");
		if (const FMetasoundFrontendClass* Class = Builder.FindDependency(Node->ClassID))
		{
			NodeClassName = Class->Metadata.GetClassName().ToString();
		}

		UE_LOG(LogMetaSound, Display, TEXT("Builder '%s' failed to find node input '%s': Node class '%s' contains no such input"), *GetName(), *InputName.ToString(), *NodeClassName);
	}
	else
	{
		UE_LOG(LogMetaSound, Display, TEXT("Builder '%s' failed to find node input '%s': Node with ID '%s' not found"), *GetName(), *InputName.ToString(), *NodeHandle.NodeID.ToString());
	}


	OutResult = EMetaSoundBuilderResult::Failed;
	return { };
}

TArray<FMetaSoundBuilderNodeInputHandle> UMetaSoundBuilderBase::FindNodeInputs(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult)
{
	return FindNodeInputsByDataType(NodeHandle, OutResult, { });
}

TArray<FMetaSoundBuilderNodeInputHandle> UMetaSoundBuilderBase::FindNodeInputsByDataType(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult, FName DataType)
{
	TArray<FMetaSoundBuilderNodeInputHandle> FoundVertices;
	if (Builder.ContainsNode(NodeHandle.NodeID))
	{
		TArray<const FMetasoundFrontendVertex*> Vertices = Builder.FindNodeInputs(NodeHandle.NodeID, DataType);
		Algo::Transform(Vertices, FoundVertices, [&NodeHandle](const FMetasoundFrontendVertex* Vertex)
		{
			return FMetaSoundBuilderNodeInputHandle(NodeHandle.NodeID, Vertex->VertexID);
		});
		OutResult = EMetaSoundBuilderResult::Succeeded;
	}
	else
	{
		UE_LOG(LogMetaSound, Display, TEXT("Failed to find node inputs by data type with builder '%s'. Node of with ID '%s' not found"), *GetName(), *NodeHandle.NodeID.ToString());
		OutResult = EMetaSoundBuilderResult::Failed;
	}

	return FoundVertices;
}

FMetaSoundBuilderNodeOutputHandle UMetaSoundBuilderBase::FindNodeOutputByName(const FMetaSoundNodeHandle& NodeHandle, FName OutputName, EMetaSoundBuilderResult& OutResult)
{
	if (const FMetasoundFrontendNode* Node = Builder.FindNode(NodeHandle.NodeID))
	{
		const TArray<FMetasoundFrontendVertex>& OutputVertices = Node->Interface.Outputs;

		auto FindByNamePredicate = [&OutputName](const FMetasoundFrontendVertex& Vertex) { return Vertex.Name == OutputName; };
		if (const FMetasoundFrontendVertex* Output = OutputVertices.FindByPredicate(FindByNamePredicate))
		{
			OutResult = EMetaSoundBuilderResult::Succeeded;
			return FMetaSoundBuilderNodeOutputHandle(Node->GetID(), Output->VertexID);
		}

		FString NodeClassName = TEXT("N/A");
		if (const FMetasoundFrontendClass* Class = Builder.FindDependency(Node->ClassID))
		{
			NodeClassName = Class->Metadata.GetClassName().ToString();
		}

		UE_LOG(LogMetaSound, Display, TEXT("Builder '%s' failed to find node output '%s': Node class '%s' contains no such output"), *GetName(), *OutputName.ToString(), *NodeClassName);
	}
	else
	{
		UE_LOG(LogMetaSound, Display, TEXT("Builder '%s' failed to find node output '%s': Node with ID '%s' not found"), *GetName(), *OutputName.ToString(), *NodeHandle.NodeID.ToString());
	}

	OutResult = EMetaSoundBuilderResult::Failed;
	return { };
}

TArray<FMetaSoundBuilderNodeOutputHandle> UMetaSoundBuilderBase::FindNodeOutputs(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult)
{
	return FindNodeOutputsByDataType(NodeHandle, OutResult, { });
}

TArray<FMetaSoundBuilderNodeOutputHandle> UMetaSoundBuilderBase::FindNodeOutputsByDataType(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult, FName DataType)
{
	TArray<FMetaSoundBuilderNodeOutputHandle> FoundVertices;
	if (Builder.ContainsNode(NodeHandle.NodeID))
	{
		TArray<const FMetasoundFrontendVertex*> Vertices = Builder.FindNodeOutputs(NodeHandle.NodeID, DataType);
		Algo::Transform(Vertices, FoundVertices, [&NodeHandle](const FMetasoundFrontendVertex* Vertex)
		{
			return FMetaSoundBuilderNodeOutputHandle(NodeHandle.NodeID, Vertex->VertexID);
		});
		OutResult = EMetaSoundBuilderResult::Succeeded;
	}
	else
	{
		UE_LOG(LogMetaSound, Display, TEXT("Failed to find node outputs by data type with builder '%s'. Node of with ID '%s' not found"), *GetName(), *NodeHandle.NodeID.ToString());
		OutResult = EMetaSoundBuilderResult::Failed;
	}

	return FoundVertices;
}

TArray<FMetaSoundNodeHandle> UMetaSoundBuilderBase::FindInterfaceInputNodes(FName InterfaceName, EMetaSoundBuilderResult& OutResult)
{
	TArray<FMetaSoundNodeHandle> NodeHandles;

	TArray<const FMetasoundFrontendNode*> Nodes;
	if (Builder.FindInterfaceInputNodes(InterfaceName, Nodes))
	{
		Algo::Transform(Nodes, NodeHandles, [this](const FMetasoundFrontendNode* Node)
		{
			check(Node);
			return FMetaSoundNodeHandle { Node->GetID() };
		});
		OutResult = EMetaSoundBuilderResult::Succeeded;
	}
	else
	{
		UE_LOG(LogMetaSound, Display, TEXT("'%s' interface not found on builder '%s'. No input nodes returned"), *InterfaceName.ToString(), *GetName());
		OutResult = EMetaSoundBuilderResult::Failed;
	}

	return NodeHandles;
}

TArray<FMetaSoundNodeHandle> UMetaSoundBuilderBase::FindInterfaceOutputNodes(FName InterfaceName, EMetaSoundBuilderResult& OutResult)
{
	TArray<FMetaSoundNodeHandle> NodeHandles;

	TArray<const FMetasoundFrontendNode*> Nodes;
	if (Builder.FindInterfaceOutputNodes(InterfaceName, Nodes))
	{
		Algo::Transform(Nodes, NodeHandles, [this](const FMetasoundFrontendNode* Node)
		{
			check(Node);
			return FMetaSoundNodeHandle { Node->GetID() };
		});
		OutResult = EMetaSoundBuilderResult::Succeeded;
	}
	else
	{
		OutResult = EMetaSoundBuilderResult::Failed;
	}

	return NodeHandles;
}

FMetaSoundNodeHandle UMetaSoundBuilderBase::FindGraphInputNode(FName InputName, EMetaSoundBuilderResult& OutResult)
{
	if (const FMetasoundFrontendNode* GraphInputNode = Builder.FindGraphInputNode(InputName))
	{
		OutResult = EMetaSoundBuilderResult::Succeeded;
		return FMetaSoundNodeHandle { GraphInputNode->GetID() };
	}

	UE_LOG(LogMetaSound, Display, TEXT("Failed to find graph input by name '%s' with builder '%s'"), *InputName.ToString(), *GetName());
	OutResult = EMetaSoundBuilderResult::Failed;
	return { };
}

FMetaSoundNodeHandle UMetaSoundBuilderBase::FindGraphOutputNode(FName OutputName, EMetaSoundBuilderResult& OutResult)
{
	if (const FMetasoundFrontendNode* GraphOutputNode = Builder.FindGraphOutputNode(OutputName))
	{
		OutResult = EMetaSoundBuilderResult::Succeeded;
		return FMetaSoundNodeHandle{ GraphOutputNode->GetID() };
	}

	UE_LOG(LogMetaSound, Display, TEXT("Failed to find graph output by name '%s' with builder '%s'"), *OutputName.ToString(), *GetName());
	OutResult = EMetaSoundBuilderResult::Failed;
	return { };
}

UMetaSoundBuilderDocument* UMetaSoundBuilderBase::CreateTransientDocumentObject() const
{
	UMetaSoundBuilderDocument* DocObject = NewObject<UMetaSoundBuilderDocument>();
	DocObject->SetBaseMetaSoundUClass(GetBuilderUClass());
	return DocObject;
}

void UMetaSoundBuilderBase::InitFrontendBuilder()
{
	UMetaSoundBuilderDocument* DocObject = CreateTransientDocumentObject();
	Builder = FMetaSoundFrontendDocumentBuilder(DocObject);
	Builder.InitDocument();
}

void UMetaSoundBuilderBase::InitNodeLocations()
{
	Builder.InitNodeLocations();
}

UObject* UMetaSoundBuilderBase::GetReferencedPresetAsset() const
{
	using namespace Metasound::Frontend;
	if (!IsPreset())
	{
		return nullptr;
	}

	// Find the single external node which is the referenced preset asset, 
	// and find the asset with its registry key 
	auto FindExternalNode = [this](const FMetasoundFrontendNode& Node) 
	{
		const FMetasoundFrontendClass* Class = Builder.FindDependency(Node.ClassID);
		return Class->Metadata.GetType() == EMetasoundFrontendClassType::External;
	};
	const FMetasoundFrontendNode* Node = Builder.GetDocument().RootGraph.Graph.Nodes.FindByPredicate(FindExternalNode);
	if (Node != nullptr)
	{
		const FMetasoundFrontendClass* NodeClass = Builder.FindDependency(Node->ClassID);
		const FNodeRegistryKey NodeClassRegistryKey = NodeRegistryKey::CreateKey(NodeClass->Metadata);
		if (FMetasoundAssetBase* Asset = IMetaSoundAssetManager::GetChecked().TryLoadAssetFromKey(NodeClassRegistryKey))
		{
			return Asset->GetOwningAsset();
		}
	}

	return nullptr;
}

bool UMetaSoundBuilderBase::InterfaceIsDeclared(FName InterfaceName) const
{
	return Builder.IsInterfaceDeclared(InterfaceName);
}

bool UMetaSoundBuilderBase::NodesAreConnected(const FMetaSoundBuilderNodeOutputHandle& OutputHandle, const FMetaSoundBuilderNodeInputHandle& InputHandle) const
{
	const FMetasoundFrontendEdge Edge = { OutputHandle.NodeID, OutputHandle.VertexID, InputHandle.NodeID, InputHandle.VertexID };
	return Builder.ContainsEdge(Edge);
}

bool UMetaSoundBuilderBase::NodeInputIsConnected(const FMetaSoundBuilderNodeInputHandle& InputHandle) const
{
	return Builder.IsNodeInputConnected(InputHandle.NodeID, InputHandle.VertexID);
}

bool UMetaSoundBuilderBase::NodeOutputIsConnected(const FMetaSoundBuilderNodeOutputHandle& OutputHandle) const
{
	return Builder.IsNodeOutputConnected(OutputHandle.NodeID, OutputHandle.VertexID);
}

bool UMetaSoundBuilderBase::IsPreset() const
{
	return Builder.IsPreset();
}

void UMetaSoundBuilderBase::ConvertFromPreset(EMetaSoundBuilderResult& OutResult)
{
	const bool bSuccess = Builder.ConvertFromPreset();
	OutResult = bSuccess ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

void UMetaSoundBuilderBase::RemoveGraphInput(FName Name, EMetaSoundBuilderResult& OutResult)
{
	const bool bRemoved = Builder.RemoveGraphInput(Name);
	OutResult = bRemoved ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

void UMetaSoundBuilderBase::RemoveGraphOutput(FName Name, EMetaSoundBuilderResult& OutResult)
{
	const bool bRemoved = Builder.RemoveGraphOutput(Name);
	OutResult = bRemoved ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

void UMetaSoundBuilderBase::RemoveInterface(FName InterfaceName, EMetaSoundBuilderResult& OutResult)
{
	const bool bInterfaceRemoved = Builder.RemoveInterface(InterfaceName);
	OutResult = bInterfaceRemoved ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

void UMetaSoundBuilderBase::RemoveNode(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult)
{
	const bool bNodeRemoved = Builder.RemoveNode(NodeHandle.NodeID);
	OutResult = bNodeRemoved ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

void UMetaSoundBuilderBase::RemoveNodeInputDefault(const FMetaSoundBuilderNodeInputHandle& InputHandle, EMetaSoundBuilderResult& OutResult)
{
	const bool bInputDefaultRemoved = Builder.RemoveNodeInputDefault(InputHandle.NodeID, InputHandle.VertexID);
	OutResult = bInputDefaultRemoved ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

#if WITH_EDITOR
void UMetaSoundBuilderBase::SetAuthor(const FString& InAuthor)
{
	Builder.SetAuthor(InAuthor);
}
#endif // WITH_EDITOR

void UMetaSoundBuilderBase::SetNodeInputDefault(const FMetaSoundBuilderNodeInputHandle& InputHandle, const FMetasoundFrontendLiteral& Literal, EMetaSoundBuilderResult& OutResult)
{
	const bool bInputDefaultSet = Builder.SetNodeInputDefault(InputHandle.NodeID, InputHandle.VertexID, Literal);
	OutResult = bInputDefaultSet ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

void UMetaSoundBuilderBase::SetGraphInputDefault(FName InputName, const FMetasoundFrontendLiteral& Literal, EMetaSoundBuilderResult& OutResult)
{
	const bool bInputDefaultSet = Builder.SetGraphInputDefault(InputName, Literal);
	OutResult = bInputDefaultSet ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

FMetaSoundNodeHandle UMetaSoundBuilderBase::FindNodeInputParent(const FMetaSoundBuilderNodeInputHandle& InputHandle, EMetaSoundBuilderResult& OutResult)
{
	if (Builder.ContainsNode(InputHandle.NodeID))
	{
		OutResult = EMetaSoundBuilderResult::Succeeded;
		return FMetaSoundNodeHandle { InputHandle.NodeID };
	}

	OutResult = EMetaSoundBuilderResult::Failed;
	return { };
}

FMetaSoundNodeHandle UMetaSoundBuilderBase::FindNodeOutputParent(const FMetaSoundBuilderNodeOutputHandle& OutputHandle, EMetaSoundBuilderResult& OutResult)
{
	if (Builder.ContainsNode(OutputHandle.NodeID))
	{
		OutResult = EMetaSoundBuilderResult::Succeeded;
		return FMetaSoundNodeHandle{ OutputHandle.NodeID };
	}

	OutResult = EMetaSoundBuilderResult::Failed;
	return { };
}

FMetasoundFrontendVersion UMetaSoundBuilderBase::FindNodeClassVersion(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult)
{
	if (const FMetasoundFrontendNode* Node = Builder.FindNode(NodeHandle.NodeID))
	{
		if (const FMetasoundFrontendClass* Class = Builder.FindDependency(Node->ClassID))
		{
			OutResult = EMetaSoundBuilderResult::Succeeded;
			return FMetasoundFrontendVersion { Class->Metadata.GetClassName().GetFullName(), Class->Metadata.GetVersion() };
		}
	}

	OutResult = EMetaSoundBuilderResult::Failed;
	return FMetasoundFrontendVersion::GetInvalid();
}

void UMetaSoundBuilderBase::GetNodeInputData(const FMetaSoundBuilderNodeInputHandle& InputHandle, FName& Name, FName& DataType, EMetaSoundBuilderResult& OutResult)
{
	if (const FMetasoundFrontendVertex* Vertex = Builder.FindNodeInput(InputHandle.NodeID, InputHandle.VertexID))
	{
		Name = Vertex->Name;
		DataType = Vertex->TypeName;
		OutResult = EMetaSoundBuilderResult::Succeeded;
	}
	else
	{
		Name = { };
		DataType = { };
		OutResult = EMetaSoundBuilderResult::Failed;
	}
}

FMetasoundFrontendLiteral UMetaSoundBuilderBase::GetNodeInputDefault(const FMetaSoundBuilderNodeInputHandle& InputHandle, EMetaSoundBuilderResult& OutResult)
{
	if (const FMetasoundFrontendLiteral* Default = Builder.GetNodeInputDefault(InputHandle.NodeID, InputHandle.VertexID))
	{
		OutResult = EMetaSoundBuilderResult::Succeeded;
		return *Default;
	}

	OutResult = EMetaSoundBuilderResult::Failed;
	return { };
}

FMetasoundFrontendLiteral UMetaSoundBuilderBase::GetNodeInputClassDefault(const FMetaSoundBuilderNodeInputHandle& InputHandle, EMetaSoundBuilderResult& OutResult)
{
	if (const FMetasoundFrontendLiteral* Default = Builder.GetNodeInputClassDefault(InputHandle.NodeID, InputHandle.VertexID))
	{
		OutResult = EMetaSoundBuilderResult::Succeeded;
		return *Default;
	}

	OutResult = EMetaSoundBuilderResult::Failed;
	return { };
}

void UMetaSoundBuilderBase::GetNodeOutputData(const FMetaSoundBuilderNodeOutputHandle& OutputHandle, FName& Name, FName& DataType, EMetaSoundBuilderResult& OutResult)
{
	if (const FMetasoundFrontendVertex* Vertex = Builder.FindNodeOutput(OutputHandle.NodeID, OutputHandle.VertexID))
	{
		Name = Vertex->Name;
		DataType = Vertex->TypeName;
		OutResult = EMetaSoundBuilderResult::Succeeded;
	}
	else
	{
		Name = { };
		DataType = { };
		OutResult = EMetaSoundBuilderResult::Failed;
	}
}

void UMetaSoundBuilderBase::ConvertToPreset(const TScriptInterface<IMetaSoundDocumentInterface>& ReferencedNodeClass, EMetaSoundBuilderResult& OutResult)
{
	const IMetaSoundDocumentInterface* ReferencedInterface = ReferencedNodeClass.GetInterface();
	check(ReferencedInterface);
	
	// Ensure the referenced node class isn't transient 
	if (Cast<UMetaSoundBuilderDocument>(ReferencedInterface))
	{
		UE_LOG(LogMetaSound, Warning, TEXT("Transient document builders cannot be referenced when converting builder '%s' to a preset. Build the referenced node class an asset first or use an existing asset instead"), *GetName());
		OutResult = EMetaSoundBuilderResult::Failed;
		return;
	}

	// Ensure the referenced node class is a matching object type 
	const UClass& BaseMetaSoundClass = ReferencedInterface->GetBaseMetaSoundUClass();
	UObject* ReferencedObject = ReferencedNodeClass.GetObject();
	if (!ReferencedObject || (ReferencedObject && !ReferencedObject->IsA(&BaseMetaSoundClass)))
	{
		UE_LOG(LogMetaSound, Warning, TEXT("The referenced node type must match the base MetaSound class when converting builder '%s' to a preset (ex. source preset must reference another source)"), *GetName());
		OutResult = EMetaSoundBuilderResult::Failed;
		return;
	}

	// Ensure the referenced node is registered
	if (FMetasoundAssetBase* ReferencedMetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(ReferencedObject))
	{
		ReferencedMetaSoundAsset->RegisterGraphWithFrontend();
	}

	const FMetasoundFrontendDocument& ReferencedDocument = ReferencedInterface->GetDocument();
	if (Builder.ConvertToPreset(ReferencedDocument))
	{
		OutResult = EMetaSoundBuilderResult::Succeeded;
	}
	else
	{
		OutResult = EMetaSoundBuilderResult::Failed;
	}
}

TScriptInterface<IMetaSoundDocumentInterface> UMetaSoundPatchBuilder::Build(UObject* Parent, const FMetaSoundBuilderOptions& InBuilderOptions) const
{
	return &BuildInternal<UMetaSoundPatch>(Parent, InBuilderOptions);
}

const UClass& UMetaSoundPatchBuilder::GetBuilderUClass() const
{
	return *UMetaSoundPatch::StaticClass();
}

UMetaSoundBuilderBase& UMetaSoundBuilderSubsystem::AttachBuilderToAssetChecked(UObject& InObject) const
{
	const UClass* BaseClass = InObject.GetClass();
	if (BaseClass == UMetaSoundSource::StaticClass())
	{
		UMetaSoundSourceBuilder* NewBuilder = AttachSourceBuilderToAsset(CastChecked<UMetaSoundSource>(&InObject));
		return *NewBuilder;
	}
	else if (BaseClass == UMetaSoundPatch::StaticClass())
	{
		UMetaSoundPatchBuilder* NewBuilder = AttachPatchBuilderToAsset(CastChecked<UMetaSoundPatch>(&InObject));
		return *NewBuilder;
	}
	else
	{
		checkf(false, TEXT("UClass '%s' is not a base MetaSound that supports attachment via the MetaSoundBuilderSubsystem"), *BaseClass->GetFullName());
		return Metasound::Engine::BuilderSubsystemPrivate::CreateTransientBuilder<UMetaSoundPatchBuilder>();
	}
}

UMetaSoundPatchBuilder* UMetaSoundBuilderSubsystem::AttachPatchBuilderToAsset(UMetaSoundPatch* InPatch) const
{
	if (InPatch)
	{
		return &AttachBuilderToAssetCheckedPrivate<UMetaSoundPatchBuilder>(InPatch);
	}

	return nullptr;
}

UMetaSoundSourceBuilder* UMetaSoundBuilderSubsystem::AttachSourceBuilderToAsset(UMetaSoundSource* InSource) const
{
	if (InSource)
	{
		UMetaSoundSourceBuilder& SourceBuilder = AttachBuilderToAssetCheckedPrivate<UMetaSoundSourceBuilder>(InSource);
		SourceBuilder.AuditionSound = InSource;
		return &SourceBuilder;
	}

	return nullptr;
}

void UMetaSoundSourceBuilder::Audition(UObject* Parent, UAudioComponent* AudioComponent, FOnCreateAuditionGeneratorHandleDelegate CreateGenerator, bool bLiveUpdatesEnabled)
{
	using namespace Metasound::Engine;
	using namespace Metasound::Engine::BuilderSubsystemPrivate;
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundSourceBuilder::Audition);

	if (!AudioComponent)
	{
		UE_LOG(LogMetaSound, Error, TEXT("Failed to audition MetaSoundBuilder '%s': No AudioComponent supplied"), *GetFullName());
		return;
	}

	if (!bIsAttached)
	{
		FMetaSoundBuilderOptions BuilderOptions;
		BuilderOptions.bAddToRegistry = true;

		if (AuditionSound.IsValid())
		{
			BuilderOptions.ExistingMetaSound = AuditionSound.Get();
			if (USoundBase* Sound = AudioComponent->GetSound())
			{
				if (Sound != AuditionSound)
				{
					UE_LOG(LogMetaSound, Warning, TEXT("MetaSoundBuilder '%s' supplied AudioComponent with unlinked sound '%s'. Stopping sound and replacing with builder's audition sound."),
						*GetFullName(),
						*Sound->GetFullName());
				}
			}
		}
		else
		{
			BuilderOptions.Name = MakeUniqueObjectName(nullptr, UMetaSoundSource::StaticClass(), FName(GetName() + TEXT("_Audition")));
		}

		AuditionSound = &BuildInternal<UMetaSoundSource>(Parent, BuilderOptions);
	}

	AudioComponent->SetSound(AuditionSound.Get());

	if (bLiveUpdatesEnabled && !BuilderSubsystemPrivate::LiveUpdateEnabledCVar)
	{
		UE_LOG(LogMetaSound, Warning, TEXT("Attempting to audition MetaSoundBuilder '%s' with live update enabled, but feature's console variable is disabled. Request is being ignored and updates will not be audible."),
			*GetFullName(),
			* AuditionSound->GetFullName());
	}

	const bool bEnableDynamicGenerators = bLiveUpdatesEnabled && BuilderSubsystemPrivate::LiveUpdateEnabledCVar;
	AuditionSound->EnableDynamicGenerators(bEnableDynamicGenerators);

	if (CreateGenerator.IsBound())
	{
		UMetasoundGeneratorHandle* NewHandle = UMetasoundGeneratorHandle::CreateMetaSoundGeneratorHandle(AudioComponent);
		checkf(NewHandle, TEXT("BindToGeneratorDelegate Failed when attempting to audition MetaSoundSource builder '%s'"), *GetName());
		CreateGenerator.Execute(NewHandle);
	}

	AudioComponent->Play();
}

bool UMetaSoundSourceBuilder::ExecuteAuditionableTransaction(FAuditionableTransaction Transaction) const
{
	using namespace Metasound::Engine::BuilderSubsystemPrivate;
	using namespace Metasound::DynamicGraph;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundSourceBuilder::ExecuteAuditionableTransaction);

	if (LiveUpdateEnabledCVar && AuditionSound.IsValid())
	{
		TSharedPtr<FDynamicOperatorTransactor> Transactor = AuditionSound->GetDynamicGeneratorTransactor();
		if (Transactor.IsValid())
		{
			return Transaction(*Transactor);
		}
	}

	return false;
}

TScriptInterface<IMetaSoundDocumentInterface> UMetaSoundSourceBuilder::Build(UObject* Parent, const FMetaSoundBuilderOptions& InBuilderOptions) const
{
	return &BuildInternal<UMetaSoundSource>(Parent, InBuilderOptions);
}

void UMetaSoundSourceBuilder::InitFrontendBuilder()
{
	using namespace Metasound::Frontend;

	TSharedRef<FDocumentModifyDelegates> DocumentDelegates = MakeShared<FDocumentModifyDelegates>();
	InitDelegates(*DocumentDelegates);

	UMetaSoundBuilderDocument* DocObject = CreateTransientDocumentObject();
	Builder = FMetaSoundFrontendDocumentBuilder(DocObject, DocumentDelegates);
	Builder.InitDocument();
}

const Metasound::Engine::FOutputAudioFormatInfoPair* UMetaSoundSourceBuilder::FindOutputAudioFormatInfo() const
{
	using namespace Metasound::Engine;

	const FOutputAudioFormatInfoMap& FormatInfo = GetOutputAudioFormatInfo();

	auto Predicate = [this](const FOutputAudioFormatInfoPair& Pair)
	{
		const FMetasoundFrontendDocument& Document = Builder.GetDocument();
		return Document.Interfaces.Contains(Pair.Value.InterfaceVersion);
	};

	return Algo::FindByPredicate(FormatInfo, Predicate);
}

const UClass& UMetaSoundSourceBuilder::GetBuilderUClass() const
{
	return *UMetaSoundSource::StaticClass();
}

bool UMetaSoundSourceBuilder::GetLiveUpdatesEnabled() const
{
	using namespace Metasound::Engine::BuilderSubsystemPrivate;

	if (!LiveUpdateEnabledCVar)
	{
		return false;
	}

	if (!AuditionSound.IsValid())
	{
		return false;
	}
	
	return AuditionSound->GetDynamicGeneratorTransactor().IsValid();
}

void UMetaSoundSourceBuilder::InitDelegates(Metasound::Frontend::FDocumentModifyDelegates& OutDocumentDelegates) const
{
	OutDocumentDelegates.EdgeDelegates.OnEdgeAdded.AddUObject(this, &UMetaSoundSourceBuilder::OnEdgeAdded);
	OutDocumentDelegates.EdgeDelegates.OnRemovingEdge.AddUObject(this, &UMetaSoundSourceBuilder::OnRemovingEdge);

	OutDocumentDelegates.InterfaceDelegates.OnInputAdded.AddUObject(this, &UMetaSoundSourceBuilder::OnInputAdded);
	OutDocumentDelegates.InterfaceDelegates.OnOutputAdded.AddUObject(this, &UMetaSoundSourceBuilder::OnOutputAdded);
	OutDocumentDelegates.InterfaceDelegates.OnRemovingInput.AddUObject(this, &UMetaSoundSourceBuilder::OnRemovingInput);
	OutDocumentDelegates.InterfaceDelegates.OnRemovingOutput.AddUObject(this, &UMetaSoundSourceBuilder::OnRemovingOutput);

	OutDocumentDelegates.NodeDelegates.OnNodeAdded.AddUObject(this, &UMetaSoundSourceBuilder::OnNodeAdded);
	OutDocumentDelegates.NodeDelegates.OnNodeInputLiteralSet.AddUObject(this, &UMetaSoundSourceBuilder::OnNodeInputLiteralSet);
	OutDocumentDelegates.NodeDelegates.OnRemovingNode.AddUObject(this, &UMetaSoundSourceBuilder::OnRemovingNode);
	OutDocumentDelegates.NodeDelegates.OnRemovingNodeInputLiteral.AddUObject(this, &UMetaSoundSourceBuilder::OnRemovingNodeInputLiteral);
}

void UMetaSoundSourceBuilder::OnEdgeAdded(int32 EdgeIndex) const
{
	using namespace Metasound::DynamicGraph;

	const FMetasoundFrontendDocument& Doc = Builder.GetDocument();
	const FMetasoundFrontendEdge& NewEdge = Doc.RootGraph.Graph.Edges[EdgeIndex];
	ExecuteAuditionableTransaction([this, &NewEdge](Metasound::DynamicGraph::FDynamicOperatorTransactor& Transactor)
	{
		const FMetaSoundFrontendDocumentBuilder& Builder = GetConstBuilder();
		const FMetasoundFrontendVertex* FromNodeOutput = Builder.FindNodeOutput(NewEdge.FromNodeID, NewEdge.FromVertexID);
		const FMetasoundFrontendVertex* ToNodeInput = Builder.FindNodeInput(NewEdge.ToNodeID, NewEdge.ToVertexID);
		if (FromNodeOutput && ToNodeInput)
		{
			Transactor.AddDataEdge(NewEdge.FromNodeID, FromNodeOutput->Name, NewEdge.ToNodeID, ToNodeInput->Name);
			return true;
		}

		return false;
	});
}

TOptional<Metasound::FAnyDataReference> UMetaSoundSourceBuilder::CreateDataReference(const Metasound::FOperatorSettings& InOperatorSettings, const Metasound::FLiteral& InLiteral, Metasound::EDataReferenceAccessType AccessType)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	// TODO: move to TFunctionRef
	const FName DataType = GetMetasoundDataTypeName<float>();
	return IDataTypeRegistry::Get().CreateDataReference(DataType, AccessType, InLiteral, InOperatorSettings);
};

void UMetaSoundSourceBuilder::OnInputAdded(int32 InputIndex) const
{
	using namespace Metasound::DynamicGraph;

	ExecuteAuditionableTransaction([this, InputIndex](FDynamicOperatorTransactor& Transactor)
	{
		using namespace Metasound;
		using namespace Metasound::Frontend;

		const FMetasoundFrontendDocument& Doc = Builder.GetDocument();
		const FMetasoundFrontendGraphClass& GraphClass = Doc.RootGraph;
		const FMetasoundFrontendClassInput& NewInput = GraphClass.Interface.Inputs[InputIndex];
		const FLiteral NewInputLiteral = NewInput.DefaultLiteral.ToLiteral(NewInput.TypeName);
		Transactor.AddInputDataDestination(NewInput.NodeID, NewInput.Name, NewInputLiteral, &UMetaSoundSourceBuilder::CreateDataReference);
		return true;
	});
}

void UMetaSoundSourceBuilder::OnNodeAdded(int32 NodeIndex) const
{
	using namespace Metasound::DynamicGraph;

	ExecuteAuditionableTransaction([this, NodeIndex](FDynamicOperatorTransactor& Transactor)
	{
		using namespace Metasound;
		using namespace Metasound::Frontend;

		const FMetasoundFrontendDocument& Doc = Builder.GetDocument();
		const FMetasoundFrontendGraphClass& GraphClass = Doc.RootGraph;
		const FMetasoundFrontendNode& AddedNode = GraphClass.Graph.Nodes[NodeIndex];
		const Metasound::FNodeInitData InitData { AddedNode.Name, AddedNode.GetID() };

		const FMetasoundFrontendClass* NodeClass = Builder.FindDependency(AddedNode.ClassID);
		checkf(NodeClass, TEXT("Node successfully added to graph but document is missing associated dependency"));

		auto MatchesVertexNodeID = [&InitData](const FMetasoundFrontendClassVertex& Vertex) { return Vertex.NodeID == InitData.InstanceID; };
		switch (NodeClass->Metadata.GetType())
		{
			case EMetasoundFrontendClassType::Input:
			case EMetasoundFrontendClassType::Output:
			case EMetasoundFrontendClassType::External:
			{
			const FNodeRegistryKey& ClassKey = NodeRegistryKey::CreateKey(NodeClass->Metadata);
				FMetasoundFrontendRegistryContainer& Registry = *FMetasoundFrontendRegistryContainer::Get();
				TUniquePtr<INode> NewNode = Registry.CreateNode(ClassKey, InitData);
				Transactor.AddNode(InitData.InstanceID, MoveTemp(NewNode));
				return true;
			}
			break;

			case EMetasoundFrontendClassType::Graph:
			case EMetasoundFrontendClassType::Invalid:
			case EMetasoundFrontendClassType::Literal:
			case EMetasoundFrontendClassType::Template:
			case EMetasoundFrontendClassType::Variable:
			case EMetasoundFrontendClassType::VariableAccessor:
			case EMetasoundFrontendClassType::VariableDeferredAccessor:
			case EMetasoundFrontendClassType::VariableMutator:
			default:
			{
				static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 10, "Possible missing EMetasoundFrontendClassType case coverage");
				UE_LOG(LogMetaSound, Error, TEXT("Builder '%s' Audition Error: Addition of node was not propagated to active runtime graph: Runtime manipualtion of node ClassType unsupported."), *GetName());
				return false;
			}
		}
	});
}

void UMetaSoundSourceBuilder::OnNodeInputLiteralSet(int32 NodeIndex, int32 VertexIndex, int32 LiteralIndex) const
{
	using namespace Metasound::DynamicGraph;

	ExecuteAuditionableTransaction([this, &NodeIndex, &VertexIndex, &LiteralIndex](FDynamicOperatorTransactor& Transactor)
	{
		using namespace Metasound;
		using namespace Metasound::Engine;
		using namespace Metasound::Frontend;

		const FMetasoundFrontendGraphClass& GraphClass = Builder.GetDocument().RootGraph;
		const FMetasoundFrontendNode& Node = GraphClass.Graph.Nodes[NodeIndex];
		const FMetasoundFrontendVertex& Input = Node.Interface.Inputs[VertexIndex];
		const FMetasoundFrontendLiteral& InputDefault = Node.InputLiterals[LiteralIndex].Value;
		TUniquePtr<INode> LiteralNode = BuilderSubsystemPrivate::CreateDynamicNodeFromFrontendLiteral(Input.TypeName, InputDefault);
		Transactor.SetValue(Node.GetID(), Input.Name, MoveTemp(LiteralNode));

		return true;
	});
}

void UMetaSoundSourceBuilder::OnOutputAdded(int32 OutputIndex) const
{
	using namespace Metasound::DynamicGraph;

	ExecuteAuditionableTransaction([this, OutputIndex](FDynamicOperatorTransactor& Transactor)
	{
		using namespace Metasound::Frontend;

		const FMetasoundFrontendDocument& Doc = Builder.GetDocument();
		const FMetasoundFrontendGraphClass& GraphClass = Doc.RootGraph;
		const FMetasoundFrontendClassOutput& NewOutput = GraphClass.Interface.Outputs[OutputIndex];

		Transactor.AddOutputDataSource(NewOutput.NodeID, NewOutput.Name);
		return true;
	});
}

void UMetaSoundSourceBuilder::OnRemovingEdge(int32 EdgeIndex) const
{
	using namespace Metasound::DynamicGraph;

	const FMetasoundFrontendDocument& Doc = Builder.GetDocument();
	const FMetasoundFrontendEdge& EdgeBeingRemoved = Doc.RootGraph.Graph.Edges[EdgeIndex];
	ExecuteAuditionableTransaction([this, EdgeBeingRemoved](FDynamicOperatorTransactor& Transactor)
	{
		using namespace Metasound;
		using namespace Metasound::Engine;

		const FMetaSoundFrontendDocumentBuilder& Builder = GetConstBuilder();
		const FMetasoundFrontendVertex* FromNodeOutput = Builder.FindNodeOutput(EdgeBeingRemoved.FromNodeID, EdgeBeingRemoved.FromVertexID);
		const FMetasoundFrontendVertex* ToNodeInput = Builder.FindNodeInput(EdgeBeingRemoved.ToNodeID, EdgeBeingRemoved.ToVertexID);
		if (FromNodeOutput && ToNodeInput)
		{
			const FMetasoundFrontendLiteral* InputDefault = Builder.GetNodeInputDefault(EdgeBeingRemoved.ToNodeID, EdgeBeingRemoved.ToVertexID);
			if (!InputDefault)
			{
				InputDefault = Builder.GetNodeInputClassDefault(EdgeBeingRemoved.ToNodeID, EdgeBeingRemoved.ToVertexID);
			}

			if (ensureAlwaysMsgf(InputDefault, TEXT("Could not dynamically assign default literal upon removing edge: literal should be assigned by either the frontend document's input or the class definition")))
			{
				TUniquePtr<INode> LiteralNode = BuilderSubsystemPrivate::CreateDynamicNodeFromFrontendLiteral(ToNodeInput->TypeName, *InputDefault);
				Transactor.RemoveDataEdge(EdgeBeingRemoved.FromNodeID, FromNodeOutput->Name, EdgeBeingRemoved.ToNodeID, ToNodeInput->Name, MoveTemp(LiteralNode));
				return true;
			}
		}

		return false;
	});
}

void UMetaSoundSourceBuilder::OnRemovingInput(int32 InputIndex) const
{
	using namespace Metasound::DynamicGraph;

	ExecuteAuditionableTransaction([this, InputIndex](FDynamicOperatorTransactor& Transactor)
	{
		using namespace Metasound::Frontend;

		const FMetasoundFrontendDocument& Doc = Builder.GetDocument();
		const FMetasoundFrontendGraphClass& GraphClass = Doc.RootGraph;
		const FMetasoundFrontendClassInput& InputBeingRemoved = GraphClass.Interface.Inputs[InputIndex];

		Transactor.RemoveInputDataDestination(InputBeingRemoved.Name);
		return true;
	});
}

void UMetaSoundSourceBuilder::OnRemovingNode(int32 NodeIndex) const
{
	using namespace Metasound::DynamicGraph;
	ExecuteAuditionableTransaction([this, NodeIndex](FDynamicOperatorTransactor& Transactor)
	{
		using namespace Metasound::Frontend;

		const FMetasoundFrontendDocument& Doc = Builder.GetDocument();
		const FMetasoundFrontendGraphClass& GraphClass = Doc.RootGraph;
		const FMetasoundFrontendNode& NodeBeingRemoved = GraphClass.Graph.Nodes[NodeIndex];
		const FGuid& NodeID = NodeBeingRemoved.GetID();
		Transactor.RemoveNode(NodeID);
		return true;
	});
}

void UMetaSoundSourceBuilder::OnRemovingNodeInputLiteral(int32 NodeIndex, int32 VertexIndex, int32 LiteralIndex) const
{
	using namespace Metasound::DynamicGraph;

	ExecuteAuditionableTransaction([this, &NodeIndex, &VertexIndex, &LiteralIndex](FDynamicOperatorTransactor& Transactor)
	{
		using namespace Metasound;
		using namespace Metasound::Engine;
		using namespace Metasound::Frontend;

		const TArray<FMetasoundFrontendNode>& Nodes = Builder.GetDocument().RootGraph.Graph.Nodes;
		const FMetasoundFrontendNode& Node = Nodes[NodeIndex];
		const FMetasoundFrontendVertex& Input = Node.Interface.Inputs[VertexIndex];

		const FMetasoundFrontendLiteral* InputDefault = Builder.GetNodeInputClassDefault(Node.GetID(), Input.VertexID);
		if (ensureAlwaysMsgf(InputDefault, TEXT("Could not dynamically assign default literal from class definition upon removing input literal: document's dependency entry invalid and has no default assigned")))
		{
			TUniquePtr<INode> LiteralNode = BuilderSubsystemPrivate::CreateDynamicNodeFromFrontendLiteral(Input.TypeName, *InputDefault);
			Transactor.SetValue(Node.GetID(), Input.Name, MoveTemp(LiteralNode));
			return true;
		}

		return false;
	});
}

void UMetaSoundSourceBuilder::OnRemovingOutput(int32 OutputIndex) const
{
	using namespace Metasound::DynamicGraph;

	ExecuteAuditionableTransaction([this, OutputIndex](FDynamicOperatorTransactor& Transactor)
	{
		using namespace Metasound::Frontend;

		const FMetasoundFrontendDocument& Doc = Builder.GetDocument();
		const FMetasoundFrontendGraphClass& GraphClass = Doc.RootGraph;
		const FMetasoundFrontendClassOutput& OutputBeingRemoved = GraphClass.Interface.Outputs[OutputIndex];

		Transactor.RemoveOutputDataSource(OutputBeingRemoved.Name);
		return true;
	});
}

void UMetaSoundSourceBuilder::SetFormat(EMetaSoundOutputAudioFormat OutputFormat, EMetaSoundBuilderResult& OutResult)
{
	using namespace Metasound::Engine;
	using namespace Metasound::Frontend;

	// Convert to non-preset MetaSoundSource since interface data is being altered
	Builder.ConvertFromPreset();

	const FOutputAudioFormatInfoMap& FormatMap = GetOutputAudioFormatInfo();

	// Determine which interfaces to add and remove from the document due to the
	// output format being changed.
	TArray<FMetasoundFrontendVersion> OutputFormatsToAdd;
	if (const FOutputAudioFormatInfo* FormatInfo = FormatMap.Find(OutputFormat))
	{
		OutputFormatsToAdd.Add(FormatInfo->InterfaceVersion);
	}

	TArray<FMetasoundFrontendVersion> OutputFormatsToRemove;

	const FMetasoundFrontendDocument& Document = GetConstBuilder().GetDocument();
	for (const FOutputAudioFormatInfoPair& Pair : FormatMap)
	{
		const FMetasoundFrontendVersion& FormatVersion = Pair.Value.InterfaceVersion;
		if (Document.Interfaces.Contains(FormatVersion))
		{
			if (!OutputFormatsToAdd.Contains(FormatVersion))
			{
				OutputFormatsToRemove.Add(FormatVersion);
			}
		}
	}

	FModifyInterfaceOptions Options(OutputFormatsToRemove, OutputFormatsToAdd);

#if WITH_EDITORONLY_DATA
	Options.bSetDefaultNodeLocations = true;
#endif // WITH_EDITORONLY_DATA

	const bool bSuccess = Builder.ModifyInterfaces(MoveTemp(Options));
	OutResult = bSuccess ? EMetaSoundBuilderResult::Succeeded : EMetaSoundBuilderResult::Failed;
}

UMetaSoundPatchBuilder* UMetaSoundBuilderSubsystem::CreatePatchBuilder(FName BuilderName, EMetaSoundBuilderResult& OutResult)
{
	return &Metasound::Engine::BuilderSubsystemPrivate::CreateTransientBuilder<UMetaSoundPatchBuilder>();
}

UMetaSoundSourceBuilder* UMetaSoundBuilderSubsystem::CreateSourceBuilder(
	FName BuilderName,
	FMetaSoundBuilderNodeOutputHandle& OnPlayNodeOutput,
	FMetaSoundBuilderNodeInputHandle& OnFinishedNodeInput,
	TArray<FMetaSoundBuilderNodeInputHandle>& AudioOutNodeInputs,
	EMetaSoundBuilderResult& OutResult,
	EMetaSoundOutputAudioFormat OutputFormat,
	bool bIsOneShot)
{
	using namespace Metasound::Frontend;
	using namespace Metasound::Engine::BuilderSubsystemPrivate;

	OnPlayNodeOutput = { };
	OnFinishedNodeInput = { };
	AudioOutNodeInputs.Reset();

	UMetaSoundSourceBuilder& NewBuilder = CreateTransientBuilder<UMetaSoundSourceBuilder>();

	OutResult = EMetaSoundBuilderResult::Succeeded;
	if (OutputFormat != EMetaSoundOutputAudioFormat::Mono)
	{
		NewBuilder.SetFormat(OutputFormat, OutResult);
	}
	
	if (OutResult == EMetaSoundBuilderResult::Succeeded)
	{
		TArray<FMetaSoundNodeHandle> AudioOutputNodes;
		if (const Metasound::Engine::FOutputAudioFormatInfoPair* FormatInfo = NewBuilder.FindOutputAudioFormatInfo())
		{
			AudioOutputNodes = NewBuilder.FindInterfaceOutputNodes(FormatInfo->Value.InterfaceVersion.Name, OutResult);
		}
		else
		{
			OutResult = EMetaSoundBuilderResult::Failed;
		}

		if (OutResult == EMetaSoundBuilderResult::Succeeded)
		{
			Algo::Transform(AudioOutputNodes, AudioOutNodeInputs, [&NewBuilder, &BuilderName](const FMetaSoundNodeHandle& AudioOutputNode) -> FMetaSoundBuilderNodeInputHandle
			{
				EMetaSoundBuilderResult Result;
				TArray<FMetaSoundBuilderNodeInputHandle> Inputs = NewBuilder.FindNodeInputs(AudioOutputNode, Result);
				if (!Inputs.IsEmpty())
				{
					return Inputs.Last();
				}

				UE_LOG(LogMetaSound, Error, TEXT("Builder '%s' Creation Error: Failed to find expected audio output node input vertex. Returned vertices set may be incomplete."), *BuilderName.ToString());
				return { };
			});
		}
		else
		{
			UE_LOG(LogMetaSound, Error, TEXT("Builder '%s' Creation Error: Failed to find expected audio output format and/or associated output nodes."), *BuilderName.ToString());
			return nullptr;
		}
	}
	else
	{
		UE_LOG(LogMetaSound, Error, TEXT("Builder '%s' Creation Error: Failed to set output format when initializing."), *BuilderName.ToString());
		return nullptr;
	}

	{
		FMetaSoundNodeHandle OnPlayNode = NewBuilder.FindGraphInputNode(SourceInterface::Inputs::OnPlay, OutResult);
		if (OutResult == EMetaSoundBuilderResult::Failed)
		{
			UE_LOG(LogMetaSound, Error, TEXT("Builder '%s' Creation Error: Failed to add required interface '%s' when attempting to create MetaSound Source Builder"), *BuilderName.ToString(), * SourceInterface::GetVersion().ToString());
			return nullptr;
		}

		TArray<FMetaSoundBuilderNodeOutputHandle> Outputs = NewBuilder.FindNodeOutputs(OnPlayNode, OutResult);
		if (OutResult == EMetaSoundBuilderResult::Failed)
		{
			UE_LOG(LogMetaSound, Error, TEXT("Builder '%s' Creation Error: Failed to find output vertex for 'OnPlay' input node when attempting to create MetaSound Source Builder"), *BuilderName.ToString());
			return nullptr;
		}

		check(!Outputs.IsEmpty());
		OnPlayNodeOutput = Outputs.Last();
	}

	if (bIsOneShot)
	{
		FMetaSoundNodeHandle OnFinishedNode = NewBuilder.FindGraphOutputNode(SourceOneShotInterface::Outputs::OnFinished, OutResult);
		if (OutResult == EMetaSoundBuilderResult::Failed)
		{
			UE_LOG(LogMetaSound, Error, TEXT("Builder '%s' Creation Error: Failed to add '%s' interface; interface definition may not be registered."), *BuilderName.ToString(), *SourceOneShotInterface::GetVersion().ToString());
		}

		TArray<FMetaSoundBuilderNodeInputHandle> Inputs = NewBuilder.FindNodeInputs(OnFinishedNode, OutResult);
		if (OutResult == EMetaSoundBuilderResult::Failed)
		{
			UE_LOG(LogMetaSound, Error, TEXT("Builder '%s' Creation Error: Failed to find input vertex for 'OnFinished' output node when attempting to create MetaSound Source Builder"), *BuilderName.ToString());
			return nullptr;
		}

		check(!Inputs.IsEmpty());
		OnFinishedNodeInput = Inputs.Last();
	}
	else
	{
		NewBuilder.RemoveInterface(SourceOneShotInterface::GetVersion().Name, OutResult);
	}

	return &NewBuilder;
}

UMetaSoundPatchBuilder* UMetaSoundBuilderSubsystem::CreatePatchPresetBuilder(FName BuilderName, const TScriptInterface<IMetaSoundDocumentInterface>& ReferencedNodeClass, EMetaSoundBuilderResult& OutResult)
{
	UMetaSoundPatchBuilder& Builder = Metasound::Engine::BuilderSubsystemPrivate::CreateTransientBuilder<UMetaSoundPatchBuilder>();
	Builder.ConvertToPreset(ReferencedNodeClass, OutResult);
	return &Builder;
}

UMetaSoundBuilderBase& UMetaSoundBuilderSubsystem::CreatePresetBuilder(FName BuilderName, const TScriptInterface<IMetaSoundDocumentInterface>& ReferencedPatchClass, EMetaSoundBuilderResult& OutResult)
{
	const UClass& Class = ReferencedPatchClass->GetBaseMetaSoundUClass();
	if (&Class == UMetaSoundSource::StaticClass())
	{
		return *CreateSourcePresetBuilder(BuilderName, ReferencedPatchClass, OutResult);
	}
	else if (&Class == UMetaSoundPatch::StaticClass())
	{
		return *CreatePatchPresetBuilder(BuilderName, ReferencedPatchClass, OutResult);
	}
	else
	{
		checkf(false, TEXT("UClass '%s' cannot be built to a MetaSound preset"), *Class.GetFullName());
		return Metasound::Engine::BuilderSubsystemPrivate::CreateTransientBuilder<UMetaSoundPatchBuilder>();
	}
}

UMetaSoundSourceBuilder* UMetaSoundBuilderSubsystem::CreateSourcePresetBuilder(FName BuilderName, const TScriptInterface<IMetaSoundDocumentInterface>& ReferencedNodeClass, EMetaSoundBuilderResult& OutResult)
{
	UMetaSoundSourceBuilder& Builder = Metasound::Engine::BuilderSubsystemPrivate::CreateTransientBuilder<UMetaSoundSourceBuilder>();
	Builder.ConvertToPreset(ReferencedNodeClass, OutResult);
	return &Builder;
}

UMetaSoundBuilderSubsystem& UMetaSoundBuilderSubsystem::GetChecked()
{
	checkf(GEngine, TEXT("Cannot access UMetaSoundBuilderSubsystem without engine loaded"));
	UMetaSoundBuilderSubsystem* BuilderSubsystem = GEngine->GetEngineSubsystem<UMetaSoundBuilderSubsystem>();
	checkf(BuilderSubsystem, TEXT("Failed to find initialized 'UMetaSoundBuilderSubsystem"));
	return *BuilderSubsystem;
}

const UMetaSoundBuilderSubsystem& UMetaSoundBuilderSubsystem::GetConstChecked()
{
	checkf(GEngine, TEXT("Cannot access UMetaSoundBuilderSubsystem without engine loaded"));
	UMetaSoundBuilderSubsystem* BuilderSubsystem = GEngine->GetEngineSubsystem<UMetaSoundBuilderSubsystem>();
	checkf(BuilderSubsystem, TEXT("Failed to find initialized 'UMetaSoundBuilderSubsystem"));
	return *BuilderSubsystem;
}

FMetasoundFrontendLiteral UMetaSoundBuilderSubsystem::CreateBoolMetaSoundLiteral(bool Value, FName& OutDataType)
{
	return Metasound::Engine::BuilderSubsystemPrivate::CreatePODMetaSoundLiteral(Value, OutDataType);
}

FMetasoundFrontendLiteral UMetaSoundBuilderSubsystem::CreateBoolArrayMetaSoundLiteral(const TArray<bool>& Value, FName& OutDataType)
{
	return Metasound::Engine::BuilderSubsystemPrivate::CreatePODMetaSoundLiteral(Value, OutDataType);
}

FMetasoundFrontendLiteral UMetaSoundBuilderSubsystem::CreateFloatMetaSoundLiteral(float Value, FName& OutDataType)
{
	return Metasound::Engine::BuilderSubsystemPrivate::CreatePODMetaSoundLiteral(Value, OutDataType);
}

FMetasoundFrontendLiteral UMetaSoundBuilderSubsystem::CreateFloatArrayMetaSoundLiteral(const TArray<float>& Value, FName& OutDataType)
{
	return Metasound::Engine::BuilderSubsystemPrivate::CreatePODMetaSoundLiteral(Value, OutDataType);
}

FMetasoundFrontendLiteral UMetaSoundBuilderSubsystem::CreateIntMetaSoundLiteral(int32 Value, FName& OutDataType)
{
	return Metasound::Engine::BuilderSubsystemPrivate::CreatePODMetaSoundLiteral(Value, OutDataType);
}

FMetasoundFrontendLiteral UMetaSoundBuilderSubsystem::CreateIntArrayMetaSoundLiteral(const TArray<int32>& Value, FName& OutDataType)
{
	return Metasound::Engine::BuilderSubsystemPrivate::CreatePODMetaSoundLiteral(Value, OutDataType);
}

FMetasoundFrontendLiteral UMetaSoundBuilderSubsystem::CreateStringMetaSoundLiteral(const FString& Value, FName& OutDataType)
{
	return Metasound::Engine::BuilderSubsystemPrivate::CreatePODMetaSoundLiteral(Value, OutDataType);
}

FMetasoundFrontendLiteral UMetaSoundBuilderSubsystem::CreateStringArrayMetaSoundLiteral(const TArray<FString>& Value, FName& OutDataType)
{
	return Metasound::Engine::BuilderSubsystemPrivate::CreatePODMetaSoundLiteral(Value, OutDataType);
}

FMetasoundFrontendLiteral UMetaSoundBuilderSubsystem::CreateObjectMetaSoundLiteral(UObject* Value)
{
	FMetasoundFrontendLiteral Literal;
	Literal.Set(Value);
	return Literal;
}

FMetasoundFrontendLiteral UMetaSoundBuilderSubsystem::CreateObjectArrayMetaSoundLiteral(const TArray<UObject*>& Value)
{
	FMetasoundFrontendLiteral Literal;
	Literal.Set(Value);
	return Literal;
}

FMetasoundFrontendLiteral UMetaSoundBuilderSubsystem::CreateMetaSoundLiteralFromParam(const FAudioParameter& Param)
{
	return FMetasoundFrontendLiteral { Param };
}

bool UMetaSoundBuilderSubsystem::DetachBuilderFromAsset(const FMetasoundFrontendClassName& InClassName)
{
	return AssetBuilders.Remove(InClassName.GetFullName()) > 0;
}

const Metasound::Frontend::FDocumentModifyDelegates* UMetaSoundBuilderSubsystem::FindModifyDelegates(const FMetasoundFrontendClassName& InClassName) const
{
	using namespace Metasound::Frontend;

	TWeakObjectPtr<UMetaSoundBuilderBase> BuilderPtr = AssetBuilders.FindRef(InClassName.GetFullName());
	if (BuilderPtr.IsValid())
	{
		return &BuilderPtr->GetConstBuilder().GetDocumentDelegates();
	}

	return nullptr;
}

UMetaSoundPatchBuilder* UMetaSoundBuilderSubsystem::FindPatchBuilder(FName BuilderName)
{
	return NamedPatchBuilders.FindRef(BuilderName);
}

UMetaSoundSourceBuilder* UMetaSoundBuilderSubsystem::FindSourceBuilder(FName BuilderName)
{
	return NamedSourceBuilders.FindRef(BuilderName);
}

void UMetaSoundBuilderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	using namespace Metasound::Frontend;

	IMetaSoundDocumentBuilderRegistry::Set([]() -> IMetaSoundDocumentBuilderRegistry&
	{
		check(GEngine);
		UMetaSoundBuilderSubsystem* BuilderSubsystem = GEngine->GetEngineSubsystem<UMetaSoundBuilderSubsystem>();
		check(BuilderSubsystem);
		return static_cast<IMetaSoundDocumentBuilderRegistry&>(*BuilderSubsystem);
	});
}

bool UMetaSoundBuilderSubsystem::IsInterfaceRegistered(FName InInterfaceName) const
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendInterface Interface;
	return ISearchEngine::Get().FindInterfaceWithHighestVersion(InInterfaceName, Interface);
}

void UMetaSoundBuilderSubsystem::RegisterPatchBuilder(FName BuilderName, UMetaSoundPatchBuilder* Builder)
{
	NamedPatchBuilders.FindOrAdd(BuilderName) = Builder;
}

void UMetaSoundBuilderSubsystem::RegisterSourceBuilder(FName BuilderName, UMetaSoundSourceBuilder* Builder)
{
	NamedSourceBuilders.FindOrAdd(BuilderName) = Builder;
}

bool UMetaSoundBuilderSubsystem::UnregisterPatchBuilder(FName BuilderName)
{
	return NamedPatchBuilders.Remove(BuilderName) > 0;
}

bool UMetaSoundBuilderSubsystem::UnregisterSourceBuilder(FName BuilderName)
{
	return NamedSourceBuilders.Remove(BuilderName) > 0;
}

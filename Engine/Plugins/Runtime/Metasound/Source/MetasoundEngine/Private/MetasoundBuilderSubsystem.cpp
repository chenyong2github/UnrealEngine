// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundBuilderSubsystem.h"

#include "Algo/Find.h"
#include "Algo/Transform.h"
#include "Components/AudioComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/MetasoundOutputFormatInterfaces.h"
#include "Interfaces/MetasoundFrontendSourceInterface.h"
#include "Metasound.h"
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

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetasoundBuilderSubsystem)


namespace Metasound::Engine
{
	namespace BuilderSubsystemPrivate
	{
		template <typename TLiteralType>
		FMetasoundFrontendLiteral CreatePODMetaSoundLiteral(const TLiteralType& Value, FName& OutDataType)
		{
			OutDataType = GetMetasoundDataTypeName<TLiteralType>();

			FMetasoundFrontendLiteral Literal;
			Literal.Set(Value);
			return Literal;
		}

		template <typename BuilderClass>
		BuilderClass& CreateTransientBuilder()
		{
			const EObjectFlags NewObjectFlags = RF_Public | RF_Transient;
			UPackage* TransientPackage = GetTransientPackage();
			const FName ObjectName = MakeUniqueObjectName(TransientPackage, BuilderClass::StaticClass());
			TObjectPtr<BuilderClass> NewBuilder = NewObject<BuilderClass>(TransientPackage, ObjectName, NewObjectFlags);
			check(NewBuilder);
			NewBuilder->CreateTransientDocument();
			return *NewBuilder.Get();
		}
	} // namespace BuilderSubsystemPrivate
} // namespace Metasound::Engine

FMetaSoundBuilderNodeOutputHandle UMetaSoundBuilderBase::AddGraphInputNode(FName Name, FName DataType, FMetasoundFrontendLiteral DefaultValue, EMetaSoundBuilderResult& OutResult, bool bIsConstructorInput)
{
	FMetasoundFrontendClassInput Description;
	Description.Name = Name;
	Description.TypeName = DataType;
	Description.NodeID = FGuid::NewGuid();
	Description.VertexID = FGuid::NewGuid();
	Description.DefaultLiteral = static_cast<FMetasoundFrontendLiteral>(DefaultValue);
	Description.AccessType = bIsConstructorInput ? EMetasoundFrontendVertexAccessType::Value : EMetasoundFrontendVertexAccessType::Reference;

	FMetaSoundBuilderNodeOutputHandle NewHandle;

	const FMetasoundFrontendNode* Node = Builder.FindGraphInputNode(Name);
	if (Node)
	{
		UE_LOG(LogMetaSound, Warning, TEXT("AddGraphInputNode Failed: Input Node already exists with name '%s'; returning handle to existing node which may or may not match requested DataType '%s'"), *Name.ToString(), *DataType.ToString());
	}
	else
	{
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

FMetaSoundBuilderNodeInputHandle UMetaSoundBuilderBase::AddGraphOutputNode(FName Name, FName DataType, FMetasoundFrontendLiteral DefaultValue, EMetaSoundBuilderResult& OutResult, bool bIsConstructorInput)
{
	FMetasoundFrontendClassOutput Description;
	Description.Name = Name;
	Description.TypeName = DataType;
	Description.NodeID = FGuid::NewGuid();
	Description.VertexID = FGuid::NewGuid();
	Description.AccessType = bIsConstructorInput ? EMetasoundFrontendVertexAccessType::Value : EMetasoundFrontendVertexAccessType::Reference;

	FMetaSoundBuilderNodeInputHandle NewHandle;
	const FMetasoundFrontendNode* Node = Builder.FindGraphOutputNode(Name);
	if (Node)
	{
		UE_LOG(LogMetaSound, Warning, TEXT("AddGraphOutputNode Failed: Output Node already exists with name '%s'; returning handle to existing node which may or may not match requested DataType '%s'"), *Name.ToString(), *DataType.ToString());
	}
	else
	{
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

FMetaSoundNodeHandle UMetaSoundBuilderBase::AddNode(TScriptInterface<IMetaSoundDocumentInterface> NodeClass, EMetaSoundBuilderResult& OutResult)
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
				FMetasoundFrontendClassMetadata NodeClassMetadata = NodeClassGraph.Metadata;
				NewHandle.NodeID = NewNode->GetID();
			}
		}
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
		return FMetaSoundBuilderNodeInputHandle { NewEdge->ToNodeID, Vertex->VertexID };
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
		return FMetaSoundBuilderNodeOutputHandle { NewEdge->ToNodeID, Vertex->VertexID };
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
			return FMetaSoundBuilderNodeInputHandle { Node->GetID(), Input->VertexID };
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
			return FMetaSoundBuilderNodeInputHandle { NodeHandle.NodeID, Vertex->VertexID };
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
			return FMetaSoundBuilderNodeOutputHandle { Node->GetID(), Output->VertexID };
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
			return FMetaSoundBuilderNodeOutputHandle { NodeHandle.NodeID, Vertex->VertexID };
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

void UMetaSoundBuilderBase::CreateTransientDocument()
{
	TObjectPtr<UMetaSoundBuilderDocument> NewBuilder = NewObject<UMetaSoundBuilderDocument>();
	TScriptInterface<IMetaSoundDocumentInterface> MetaSoundSourceDocInterface = NewBuilder;
	Builder = FMetaSoundFrontendDocumentBuilder(MetaSoundSourceDocInterface);
	Builder.InitDocument();
}

const UClass& UMetaSoundBuilderBase::GetBaseMetaSoundUClass() const
{
	return Builder.GetDocumentInterface().GetBaseMetaSoundUClass();
}

void UMetaSoundBuilderBase::InitNodeLocations()
{
	Builder.InitNodeLocations();
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

#if WITH_EDITOR
void UMetaSoundBuilderBase::SetAuthor(const FString& InAuthor)
{
	Builder.SetAuthor(InAuthor);
}
#endif // WITH_EDITOR

void UMetaSoundBuilderBase::SetNodeInputDefault(const FMetaSoundBuilderNodeInputHandle& InputHandle, const FMetasoundFrontendLiteral& Literal, EMetaSoundBuilderResult& OutResult)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	const bool bInputDefaultSet = Builder.SetNodeInputDefault(InputHandle.NodeID, InputHandle.VertexID, Literal);
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

TScriptInterface<IMetaSoundDocumentInterface> UMetaSoundPatchBuilder::Build(UObject* Parent, const FMetaSoundBuilderOptions& InBuilderOptions) const
{
	return BuildInternal<UMetaSoundPatch>(Parent, InBuilderOptions);
}

void UMetaSoundSourceBuilder::Audition(UObject* Parent, UAudioComponent* AudioComponent, FOnCreateAuditionGeneratorHandleDelegate CreateGenerator)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundSourceBuilder::Audition);

	if (!AudioComponent)
	{
		UE_LOG(LogMetaSound, Error, TEXT("Failed to audition MetaSoundBuilder '%s': No AudioComponent supplied"), *GetFullName());
		return;
	}

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

	AuditionSound = BuildInternal<UMetaSoundSource>(Parent, BuilderOptions);
	if (AuditionSound.IsValid())
	{
		AudioComponent->SetSound(AuditionSound.Get());
	}

	if (CreateGenerator.IsBound())
	{
		UMetasoundGeneratorHandle* NewHandle = UMetasoundGeneratorHandle::CreateMetaSoundGeneratorHandle(AudioComponent);
		checkf(NewHandle, TEXT("BindToGeneratorDelegate Failed when attempting to audition MetaSoundSource builder '%s'"), *GetName());
		CreateGenerator.Execute(NewHandle);
	}

	AudioComponent->Play();
}

TScriptInterface<IMetaSoundDocumentInterface> UMetaSoundSourceBuilder::Build(UObject* Parent, const FMetaSoundBuilderOptions& InBuilderOptions) const
{
	return BuildInternal<UMetaSoundSource>(Parent, InBuilderOptions);
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

	const bool bSuccess = Builder.ModifyInterfaces(OutputFormatsToRemove, OutputFormatsToAdd);
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

	OutResult = EMetaSoundBuilderResult::Failed;

	UMetaSoundSourceBuilder& NewBuilder = CreateTransientBuilder<UMetaSoundSourceBuilder>();

	bool bFormatSet = OutputFormat == EMetaSoundOutputAudioFormat::Mono;
	if (!bFormatSet)
	{
		EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
		NewBuilder.SetFormat(OutputFormat, Result);
		bFormatSet = Result == EMetaSoundBuilderResult::Succeeded;
	}

	if (bFormatSet)
	{
		const Metasound::Engine::FOutputAudioFormatInfoPair* FormatInfo = NewBuilder.FindOutputAudioFormatInfo();
		TArray<FMetaSoundNodeHandle> AudioOutputNodes;
		if (FormatInfo)
		{
			AudioOutputNodes = NewBuilder.FindInterfaceOutputNodes(FormatInfo->Value.InterfaceVersion.Name, OutResult);
		}

		if (!AudioOutputNodes.IsEmpty())
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
		UE_LOG(LogMetaSound, Error, TEXT("Builder '%s' Creation Error: Failed to set output audio output format when initializing."), *BuilderName.ToString());
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

	{
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
	}

	OutResult = EMetaSoundBuilderResult::Succeeded;
	return &NewBuilder;
}

UMetaSoundSourceBuilder* UMetaSoundBuilderSubsystem::AttachSourceBuilderToAsset(UMetaSoundSource* InSource) const
{
	if (InSource)
	{
		TObjectPtr<UMetaSoundSourceBuilder> NewBuilder = NewObject<UMetaSoundSourceBuilder>(InSource);
		TScriptInterface<IMetaSoundDocumentInterface> MetaSoundSourceDocInterface = InSource;
		NewBuilder->Builder = FMetaSoundFrontendDocumentBuilder(MetaSoundSourceDocInterface);
		return NewBuilder;
	}

	return nullptr;
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

UMetaSoundPatchBuilder* UMetaSoundBuilderSubsystem::FindPatchBuilder(FName BuilderName)
{
	return PatchBuilders.FindRef(BuilderName);
}

UMetaSoundSourceBuilder* UMetaSoundBuilderSubsystem::FindSourceBuilder(FName BuilderName)
{
	return SourceBuilders.FindRef(BuilderName);
}

bool UMetaSoundBuilderSubsystem::IsInterfaceRegistered(FName InInterfaceName) const
{
	using namespace Metasound::Frontend;

	FMetasoundFrontendInterface Interface;
	return ISearchEngine::Get().FindInterfaceWithHighestVersion(InInterfaceName, Interface);
}

void UMetaSoundBuilderSubsystem::RegisterPatchBuilder(FName BuilderName, UMetaSoundPatchBuilder* Builder)
{
	PatchBuilders.FindOrAdd(BuilderName) = Builder;
}

void UMetaSoundBuilderSubsystem::RegisterSourceBuilder(FName BuilderName, UMetaSoundSourceBuilder* Builder)
{
	SourceBuilders.FindOrAdd(BuilderName) = Builder;
}

bool UMetaSoundBuilderSubsystem::UnregisterPatchBuilder(FName BuilderName)
{
	return PatchBuilders.Remove(BuilderName) > 0;
}

bool UMetaSoundBuilderSubsystem::UnregisterSourceBuilder(FName BuilderName)
{
	return SourceBuilders.Remove(BuilderName) > 0;
}

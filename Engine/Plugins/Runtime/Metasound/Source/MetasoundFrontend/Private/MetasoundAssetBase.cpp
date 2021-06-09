// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetBase.h"

#include "Algo/AnyOf.h"
#include "HAL/FileManager.h"
#include "Internationalization/Text.h"
#include "IStructSerializerBackend.h"
#include "MetasoundArchetype.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendGraph.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundJsonBackend.h"
#include "MetasoundLog.h"
#include "MetasoundReceiveNode.h"
#include "StructSerializer.h"

#define LOCTEXT_NAMESPACE "MetaSound"

const FString FMetasoundAssetBase::FileExtension(TEXT(".metasound"));

void FMetasoundAssetBase::RegisterGraphWithFrontend()
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FConstDocumentAccessPtr DocumentPtr = GetDocument();
	FString AssetName;
	if (const UObject* OwningAsset = GetOwningAsset())
	{
		AssetName = OwningAsset->GetName();
	}


	// Function for creating a core INode.
	FCreateMetasoundNodeFunction CreateNode = [Name=AssetName, DocumentPtr=DocumentPtr](const Metasound::FNodeInitData& InInitData) -> TUniquePtr<INode>
	{
		check(IsInGameThread());

		if (const FMetasoundFrontendDocument* Document = DocumentPtr.Get())
		{
			return FFrontendGraphBuilder().CreateGraph(*Document);
		}
		else
		{
			UE_LOG(LogMetaSound, Error, TEXT("Failed to create node for MetaSoundSource [Name:%s]"), *Name);
			return TUniquePtr<INode>(nullptr);
		}
	};

	// Function for creating a frontend class.
	FCreateMetasoundFrontendClassFunction CreateFrontendClass = [Name=AssetName, DocumentPtr=DocumentPtr]() -> FMetasoundFrontendClass
	{
		check(IsInGameThread());

		FMetasoundFrontendClass FrontendClass;
		if (const FMetasoundFrontendDocument* Document = DocumentPtr.Get())
		{
			FrontendClass = Document->RootGraph;
			FrontendClass.Metadata.Type = EMetasoundFrontendClassType::External;
		}
		else
		{
			UE_LOG(LogMetaSound, Error, TEXT("Failed to create frontend class for MetaSoundSource [Name:%s]"), *Name);
		}

		return FrontendClass;
	};

	if (IsValidNodeRegistryKey(RegistryKey))
	{
		// Unregister prior version if it exists.
		ensure(FMetasoundFrontendRegistryContainer::Get()->UnregisterExternalNode(RegistryKey));
	}

	RegistryKey = FMetasoundFrontendRegistryContainer::Get()->RegisterExternalNode(MoveTemp(CreateNode), MoveTemp(CreateFrontendClass));

	if (!IsValidNodeRegistryKey(RegistryKey))
	{
		UE_LOG(LogMetaSound, Error, TEXT("Failed to register node for MetaSoundSource [Name:%s]"), *AssetName);
	}
}

void FMetasoundAssetBase::SetMetadata(FMetasoundFrontendClassMetadata& InMetadata)
{
	FMetasoundFrontendDocument& Doc = GetDocumentChecked();
	Doc.RootGraph.Metadata = InMetadata;

	if (Doc.RootGraph.Metadata.Type != EMetasoundFrontendClassType::Graph)
	{
		UE_LOG(LogMetaSound, Display, TEXT("Forcing class type to EMetasoundFrontendClassType::Graph on root graph metadata"));
		Doc.RootGraph.Metadata.Type = EMetasoundFrontendClassType::Graph;
	}

	MarkMetasoundDocumentDirty();
}

bool FMetasoundAssetBase::IsArchetypeSupported(const FMetasoundFrontendArchetype& InArchetype) const
{
	auto IsEqualArchetype = [&](const FMetasoundFrontendArchetype& SupportedArchetype)
	{
		return Metasound::Frontend::IsEqualArchetype(InArchetype, SupportedArchetype);
	};

	return Algo::AnyOf(GetPreferredArchetypes(), IsEqualArchetype);
}

const FMetasoundFrontendArchetype& FMetasoundAssetBase::GetPreferredArchetypes(const FMetasoundFrontendDocument& InDocument, const FMetasoundFrontendArchetype& InDefaultArchetype) const
{
	// Default to archetype provided in case it is supported.
	if (IsArchetypeSupported(InDefaultArchetype))
	{
		return InDefaultArchetype;
	}

	// If existing archetype is not supported, get the most similar that still supports the documents environment.
	const FMetasoundFrontendArchetype* SimilarArchetype = Metasound::Frontend::FindMostSimilarArchetypeSupportingEnvironment(InDocument, GetPreferredArchetypes());

	if (nullptr != SimilarArchetype)
	{
		return *SimilarArchetype;
	}

	// Nothing found. Return the existing archetype for the FMetasoundAssetBase.
	return GetArchetype();
}

void FMetasoundAssetBase::SetDocument(const FMetasoundFrontendDocument& InDocument)
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	Document = InDocument;
}

void FMetasoundAssetBase::ConformDocumentToArchetype()
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	const FMetasoundFrontendArchetype& Archetype = GetArchetype();
	Metasound::Frontend::FMatchRootGraphToArchetype Transform(Archetype);
	Transform.Transform(GetDocumentHandle());

	MarkMetasoundDocumentDirty();
}

bool FMetasoundAssetBase::CopyDocumentAndInjectReceiveNodes(uint64 InInstanceID, const FMetasoundFrontendDocument& InSourceDoc, FMetasoundFrontendDocument& OutDestDoc) const
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	OutDestDoc = InSourceDoc;

	FDocumentHandle Document = IDocumentController::CreateDocumentHandle(MakeAccessPtr<FDocumentAccessPtr>(OutDestDoc.AccessPoint, OutDestDoc));
	FGraphHandle RootGraph = Document->GetRootGraph();

	TArray<FSendInfoAndVertexName> SendInfoAndVertexes = GetSendInfos(InInstanceID);

	// Inject receive nodes for each transmittable input
	for (const FSendInfoAndVertexName& InfoAndVertexName : SendInfoAndVertexes)
	{

		// Add receive node to graph
		FMetasoundFrontendClassMetadata ReceiveNodeMetadata;
		bool bSuccess = GetReceiveNodeMetadataForDataType(InfoAndVertexName.SendInfo.TypeName, ReceiveNodeMetadata);
		if (!bSuccess)
		{
			// TODO: log warning
			continue;
		}
		FNodeHandle ReceiveNode = RootGraph->AddNode(ReceiveNodeMetadata);

		// Add receive node address to graph
		FNodeHandle AddressNode = AddInputPinForSendAddress(InfoAndVertexName.SendInfo, RootGraph);
		TArray<FOutputHandle> AddressNodeOutputs = AddressNode->GetOutputs();
		if (AddressNodeOutputs.Num() != 1)
		{
			// TODO: log warning
			continue;
		}

		FOutputHandle AddressOutput = AddressNodeOutputs[0];
		TArray<FInputHandle> ReceiveAddressInput = ReceiveNode->GetInputsWithVertexName(Metasound::FReceiveNodeNames::GetAddressInputName());
		if (ReceiveAddressInput.Num() != 1)
		{
			// TODO: log error
			continue;
		}

		ensure(ReceiveAddressInput[0]->Connect(*AddressOutput));

		// Swap input node connections with receive node connections
		FNodeHandle InputNode = RootGraph->GetInputNodeWithName(InfoAndVertexName.VertexName);
		if (!ensure(InputNode->GetOutputs().Num() == 1))
		{
			// TODO: handle input node with varying number of outputs or varying output types.
			continue;
		}

		FOutputHandle InputNodeOutput = InputNode->GetOutputs()[0];

		if (ensure(ReceiveNode->IsValid()))
		{
			TArray<FOutputHandle> ReceiveNodeOutputs = ReceiveNode->GetOutputs();
			if (!ensure(ReceiveNodeOutputs.Num() == 1))
			{
				// TODO: handle array outputs and receive nodes of varying formats.
				continue;
			}

			TArray<FInputHandle> ReceiveDefaultInputs = ReceiveNode->GetInputsWithVertexName(Metasound::FReceiveNodeNames::GetDefaultDataInputName());
			if (ensure(ReceiveDefaultInputs.Num() == 1))
			{
				FOutputHandle ReceiverNodeOutput = ReceiveNodeOutputs[0];
				for (FInputHandle NodeInput : InputNodeOutput->GetConnectedInputs())
				{
					// Swap connections to receiver node
					ensure(InputNodeOutput->Disconnect(*NodeInput));
					ensure(ReceiverNodeOutput->Connect(*NodeInput));
				}

				ReceiveDefaultInputs[0]->Connect(*InputNodeOutput);
			}
		}
	}

	return true;
}

Metasound::FSendAddress FMetasoundAssetBase::CreateSendAddress(uint64 InInstanceID, const FString& InVertexName, const FName& InDataTypeName) const
{
	using namespace Metasound;

	FSendAddress Address;

	Address.Subsystem = GetSubsystemNameForSendScope(ETransmissionScope::Global);
	Address.ChannelName = FName(FString::Printf(TEXT("%d:%s:%s"), InInstanceID, *InVertexName, *InDataTypeName.ToString()));

	return Address;
}

TArray<FMetasoundAssetBase::FSendInfoAndVertexName> FMetasoundAssetBase::GetSendInfos(uint64 InInstanceID) const
{
	using FSendInfo = Metasound::FMetasoundInstanceTransmitter::FSendInfo;
	using namespace Metasound::Frontend;

	TArray<FSendInfoAndVertexName> SendInfos;

	FConstGraphHandle RootGraph = GetRootGraphHandle();

	TArray<FString> SendVertices = GetTransmittableInputVertexNames();
	for (const FString& VertexName : SendVertices)
	{
		FConstNodeHandle InputNode = RootGraph->GetInputNodeWithName(VertexName);
		for (FConstInputHandle InputHandle : InputNode->GetConstInputs())
		{
			FSendInfoAndVertexName Info;

			// TODO: incorporate VertexID into address. But need to ensure that VertexID
			// will be maintained after injecting Receive nodes. 
			Info.SendInfo.Address = CreateSendAddress(InInstanceID, InputHandle->GetName(), InputHandle->GetDataType());
			Info.SendInfo.ParameterName = FName(InputHandle->GetDisplayName().ToString()); // TODO: display name hack. Need to have naming consistent in editor for inputs
			//Info.SendInfo.ParameterName = FName(*InputHandle->GetName()); // TODO: this is the expected parameter name.
			Info.SendInfo.TypeName = InputHandle->GetDataType();
			Info.VertexName = VertexName;

			SendInfos.Add(Info);
		}
	}

	return SendInfos;
}

TArray<FString> FMetasoundAssetBase::GetTransmittableInputVertexNames() const
{
	using namespace Metasound;

	// Unused inputs are all input vertices that are not in the archetype.
	TArray<FString> ArchetypeInputVertexNames;
	for (const FMetasoundFrontendClassVertex& Vertex : GetArchetype().Interface.Inputs)
	{
		ArchetypeInputVertexNames.Add(Vertex.Name);
	}

	Frontend::FConstGraphHandle RootGraph = GetRootGraphHandle();
	TArray<FString> GraphInputVertexNames = RootGraph->GetInputVertexNames();

	// Filter graph inputs by archetype inputs.
	GraphInputVertexNames = GraphInputVertexNames.FilterByPredicate([&](const FString& InName) { return !ArchetypeInputVertexNames.Contains(InName); });

	auto IsDataTypeTransmittable = [&](const FString& InVertexName)
	{
		Frontend::FConstClassInputAccessPtr ClassInputPtr = RootGraph->FindClassInputWithName(InVertexName);
		if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
		{
			FDataTypeRegistryInfo TypeInfo;
			if (Frontend::GetTraitsForDataType(ClassInput->TypeName, TypeInfo))
			{
				if (TypeInfo.bIsTransmittable)
				{
					// TODO: Currently values set directly on node pins are represented
					// as input nodes in the graph. They should not be used for transmission
					// as the number of input nodes increases quickly as more nodes
					// are added to a graph. Connecting these input nodes to the 
					// transmission system is relatively expensive. These undesirable input nodes are
					// filtered out by ignoring input nodes which are not "Visible". 
					Frontend::FConstNodeHandle InputNode = RootGraph->GetNodeWithID(ClassInput->NodeID);
					if (InputNode->IsValid())
					{
						return true;
					}
				}
			}
		}
		return false;
	};

	GraphInputVertexNames = GraphInputVertexNames.FilterByPredicate(IsDataTypeTransmittable);

	return GraphInputVertexNames;
}

Metasound::Frontend::FNodeHandle FMetasoundAssetBase::AddInputPinForSendAddress(const Metasound::FMetasoundInstanceTransmitter::FSendInfo& InSendInfo, Metasound::Frontend::FGraphHandle InGraph) const
{
	FMetasoundFrontendClassInput Description;
	FGuid VertexID = FGuid::NewGuid();

	Description.Name = InSendInfo.Address.ChannelName.ToString();
	Description.TypeName = Metasound::GetMetasoundDataTypeName<Metasound::FSendAddress>();
	Description.Metadata.Description = FText::GetEmpty();
	Description.VertexID = VertexID;
	Description.DefaultLiteral.Set(InSendInfo.Address.ChannelName.ToString());

	return InGraph->AddInputVertex(Description);
}

#if WITH_EDITORONLY_DATA
FText FMetasoundAssetBase::GetDisplayName(FString&& InTypeName) const
{
	using namespace Metasound::Frontend;

	FConstGraphHandle GraphHandle = GetRootGraphHandle();
	const bool bIsPreset = !GraphHandle->GetGraphStyle().bIsGraphEditable;

	if (!bIsPreset)
	{
		return FText::FromString(MoveTemp(InTypeName));
	}

	return FText::Format(LOCTEXT("PresetDisplayNameFormat", "{0} (Preset)"), FText::FromString(MoveTemp(InTypeName)));
}
#endif // WITH_EDITORONLY_DATA

bool FMetasoundAssetBase::GetReceiveNodeMetadataForDataType(const FName& InTypeName, FMetasoundFrontendClassMetadata& OutMetadata) const
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	const FNodeClassName ClassName = FReceiveNodeNames::GetClassNameForDataType(InTypeName);
	TArray<FMetasoundFrontendClass> ReceiverNodeClasses = ISearchEngine::Get().FindClassesWithName(ClassName, true /* bInSortByVersion */);

	if (ReceiverNodeClasses.IsEmpty())
	{
		return false;
	}

	OutMetadata = ReceiverNodeClasses[0].Metadata;
	return true;
}

void FMetasoundAssetBase::UpdateAssetTags(FMetasoundFrontendClassAssetTags& OutTags)
{
	using namespace Metasound::Frontend;

	FGraphHandle GraphHandle = GetRootGraphHandle();

	OutTags.ID = GraphHandle->GetClassID();

	OutTags.Name = GraphHandle->GetGraphMetadata().ClassName.Name;
	OutTags.Namespace = GraphHandle->GetGraphMetadata().ClassName.Namespace;
	OutTags.Variant = GraphHandle->GetGraphMetadata().ClassName.Variant;

	OutTags.MajorVersion = GraphHandle->GetGraphMetadata().Version.Major;
	OutTags.MinorVersion = GraphHandle->GetGraphMetadata().Version.Minor;


}

bool FMetasoundAssetBase::MarkMetasoundDocumentDirty() const
{
	if (const UObject* OwningAsset = GetOwningAsset())
	{
		return ensure(OwningAsset->MarkPackageDirty());
	}
	return false;
}

FMetasoundFrontendClassMetadata FMetasoundAssetBase::GetMetadata()
{
	return GetDocumentChecked().RootGraph.Metadata;
}

Metasound::Frontend::FDocumentHandle FMetasoundAssetBase::GetDocumentHandle()
{
	return Metasound::Frontend::IDocumentController::CreateDocumentHandle(GetDocument());
}

Metasound::Frontend::FConstDocumentHandle FMetasoundAssetBase::GetDocumentHandle() const
{
	return Metasound::Frontend::IDocumentController::CreateDocumentHandle(GetDocument());
}

Metasound::Frontend::FGraphHandle FMetasoundAssetBase::GetRootGraphHandle()
{
	return GetDocumentHandle()->GetRootGraph();
}

Metasound::Frontend::FConstGraphHandle FMetasoundAssetBase::GetRootGraphHandle() const
{
	return GetDocumentHandle()->GetRootGraph();
}

bool FMetasoundAssetBase::ImportFromJSON(const FString& InJSON)
{
	Metasound::Frontend::FDocumentAccessPtr Document = GetDocument();
	if (ensure(Document.IsValid()))
	{
		bool bSuccess = Metasound::Frontend::ImportJSONToMetasound(InJSON, *Document);

		if (bSuccess)
		{
			ensure(MarkMetasoundDocumentDirty());
		}

		return bSuccess;
	}
	return false;
}

bool FMetasoundAssetBase::ImportFromJSONAsset(const FString& InAbsolutePath)
{
	Metasound::Frontend::FDocumentAccessPtr Document = GetDocument();
	if (ensure(Document.IsValid()))
	{
		bool bSuccess = Metasound::Frontend::ImportJSONAssetToMetasound(InAbsolutePath, *Document);

		if (bSuccess)
		{
			ensure(MarkMetasoundDocumentDirty());
		}

		return bSuccess;
	}
	return false;
}

FMetasoundFrontendDocument& FMetasoundAssetBase::GetDocumentChecked()
{
	Metasound::Frontend::FDocumentAccessPtr DocAccessPtr = GetDocument();

	check(DocAccessPtr.IsValid());
	return *DocAccessPtr;
}

const FMetasoundFrontendDocument& FMetasoundAssetBase::GetDocumentChecked() const
{
	Metasound::Frontend::FConstDocumentAccessPtr DocAccessPtr = GetDocument();

	check(DocAccessPtr.IsValid());
	return *DocAccessPtr;
}
#undef LOCTEXT_NAMESPACE // "MetaSound"

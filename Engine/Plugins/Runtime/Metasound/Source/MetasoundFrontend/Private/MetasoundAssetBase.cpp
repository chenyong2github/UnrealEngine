// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetBase.h"

#include "Algo/AnyOf.h"
#include "Algo/Transform.h"
#include "HAL/FileManager.h"
#include "Internationalization/Text.h"
#include "IStructSerializerBackend.h"
#include "MetasoundArchetype.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendGraph.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundJsonBackend.h"
#include "MetasoundLog.h"
#include "MetasoundReceiveNode.h"
#include "StructSerializer.h"
#include "UObject/MetaData.h"

#define LOCTEXT_NAMESPACE "MetaSound"

namespace Metasound
{
	namespace AssetTags
	{
		const FString ArrayDelim = TEXT(",");

		const FName AssetClassID = "AssetClassID";
		const FName RegistryVersionMajor = "RegistryVersionMajor";
		const FName RegistryVersionMinor = "RegistryVersionMinor";

#if WITH_EDITORONLY_DATA
		const FName RegistryInputTypes = "RegistryInputTypes";
		const FName RegistryOutputTypes = "RegistryOutputTypes";
#endif // WITH_EDITORONLY_DATA
	} // namespace AssetTags
} // namespace Metasound

const FString FMetasoundAssetBase::FileExtension(TEXT(".metasound"));

void FMetasoundAssetBase::RegisterGraphWithFrontend()
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FConstDocumentAccessPtr DocumentPtr = GetDocument();
	FString AssetName;
	FString AssetPath;
	if (const UObject* OwningAsset = GetOwningAsset())
	{
		AssetName = OwningAsset->GetName();
		AssetPath = OwningAsset->GetPathName();
	}

	// Registers node by copying document. Updates to document require re-registration.
	class FNodeRegistryEntry : public INodeRegistryEntry
	{
	public:
		FNodeRegistryEntry(const FString& InName, const FMetasoundFrontendDocument& InDocument, FName InAssetPath)
		: Name(InName)
		, Document(InDocument)
		{
			// Copy frontend class to preserve original document.
			FrontendClass = Document.RootGraph;
			FrontendClass.Metadata.Type = EMetasoundFrontendClassType::External;
			ClassInfo = FNodeClassInfo(Document.RootGraph, InAssetPath);
		}

		virtual ~FNodeRegistryEntry() = default;

		virtual const FNodeClassInfo& GetClassInfo() const override
		{
			return ClassInfo;
		}

		virtual TUniquePtr<INode> CreateNode(FDefaultNodeConstructorParams&&) const override
		{
			return nullptr;
		}

		virtual TUniquePtr<INode> CreateNode(FDefaultLiteralNodeConstructorParams&&) const override
		{
			return nullptr;
		}

		virtual TUniquePtr<INode> CreateNode(const FNodeInitData&) const override
		{
			return FFrontendGraphBuilder().CreateGraph(Document);
		}

		virtual const FMetasoundFrontendClass& GetFrontendClass() const override
		{
			return FrontendClass;
		}

		virtual TUniquePtr<INodeRegistryEntry> Clone() const override
		{
			return MakeUnique<FNodeRegistryEntry>(Name, Document, ClassInfo.AssetPath);
		}

	private:
		
		FString Name;
		FMetasoundFrontendDocument Document;
		FMetasoundFrontendClass FrontendClass;
		FNodeClassInfo ClassInfo;
	};

	FNodeClassInfo ClassInfo = GetAssetClassInfo();
	FNodeRegistryKey RegistryKey = FMetasoundFrontendRegistryContainer::Get()->GetRegistryKey(ClassInfo);

	if (IsValidNodeRegistryKey(RegistryKey))
	{
		// Unregister prior version if it exists.
		if (bHasRegistered)
		{
			ensure(FMetasoundFrontendRegistryContainer::Get()->UnregisterNode(RegistryKey));
		}
	}

	if (const FMetasoundFrontendDocument* Doc = DocumentPtr.Get())
	{
		RegistryKey = FMetasoundFrontendRegistryContainer::Get()->RegisterNode(MakeUnique<FNodeRegistryEntry>(AssetName, *Doc, ClassInfo.AssetPath));
	}

	if (IsValidNodeRegistryKey(RegistryKey))
	{
		bHasRegistered = true;

#if WITH_EDITORONLY_DATA
		// Refresh Asset Registry Info if successfully registered with Frontend
		FConstGraphHandle GraphHandle = GetDocumentHandle()->GetRootGraph();
		const FMetasoundFrontendClassMetadata& ClassFrontendMetadata = GraphHandle->GetGraphMetadata();
		ClassInfo.AssetClassID = FGuid(ClassFrontendMetadata.ClassName.Name.ToString());
		FNodeClassName ClassName = ClassFrontendMetadata.ClassName.ToNodeClassName();
		FMetasoundFrontendClass GraphClass;
		ensure(ISearchEngine::Get().FindClassWithMajorVersion(ClassName, ClassFrontendMetadata.Version.Major, GraphClass));

		ClassInfo.Version = ClassFrontendMetadata.Version;

		ClassInfo.InputTypes.Reset();
		Algo::Transform(GraphClass.Interface.Inputs, ClassInfo.InputTypes, [] (const FMetasoundFrontendClassInput& Input) { return Input.TypeName; });

		ClassInfo.OutputTypes.Reset();
		Algo::Transform(GraphClass.Interface.Outputs, ClassInfo.OutputTypes, [](const FMetasoundFrontendClassOutput& Output) { return Output.TypeName; });

		SetRegistryAssetClassInfo(MoveTemp(ClassInfo));
#endif // WITH_EDITORONLY_DATA
	}
	else
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

bool FMetasoundAssetBase::IsArchetypeSupported(const FMetasoundFrontendVersion& InArchetypeVersion) const
{
	return GetSupportedArchetypeVersions().Contains(InArchetypeVersion);
}

FMetasoundFrontendVersion FMetasoundAssetBase::GetPreferredArchetypeVersion(const FMetasoundFrontendDocument& InDocument) const
{
	using namespace Metasound::Frontend;

	// Default to archetype provided in case it is supported.
	if (IsArchetypeSupported(InDocument.ArchetypeVersion))
	{
		return InDocument.ArchetypeVersion;
	}

	// If existing archetype is not supported, check if an updated version is preferred.
	auto IsNewVersion = [&](const FMetasoundFrontendVersion& InVersion)
	{
		return (InDocument.ArchetypeVersion.Name == InVersion.Name) && (InVersion.Number > InDocument.ArchetypeVersion.Number);
	};
	TArray<FMetasoundFrontendVersion> UpdatedVersions = GetSupportedArchetypeVersions().FilterByPredicate(IsNewVersion);
	if (UpdatedVersions.Num() > 0)
	{
		UpdatedVersions.Sort();
		return UpdatedVersions.Last();
	}
	
	// If existing archetype is not supported, get the most similar that still supports the documents environment.
	TArray<FMetasoundFrontendArchetype> Archetypes;
	for (const FMetasoundFrontendVersion& Version : GetSupportedArchetypeVersions())
	{
		const FArchetypeRegistryKey Key = GetArchetypeRegistryKey(Version);
		FMetasoundFrontendArchetype Archetype;
		if (IArchetypeRegistry::Get().FindArchetype(Key, Archetype))
		{
			Archetypes.Add(MoveTemp(Archetype));
		}
	}
	const FMetasoundFrontendArchetype* SimilarArchetype = Metasound::Frontend::FindMostSimilarArchetypeSupportingEnvironment(InDocument, Archetypes);

	if (nullptr != SimilarArchetype)
	{
		return SimilarArchetype->Version;
	}

	// Nothing found. Return the existing archetype for the FMetasoundAssetBase.
	return GetDefaultArchetypeVersion();
}

void FMetasoundAssetBase::SetDocument(const FMetasoundFrontendDocument& InDocument)
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	Document = InDocument;
	MarkMetasoundDocumentDirty();
}

void FMetasoundAssetBase::ConformDocumentToArchetype()
{
	FMetasoundFrontendDocument& Doc = GetDocumentChecked();

	FMetasoundFrontendVersion PreferredArchetypeVersion = GetPreferredArchetypeVersion(Doc);

	if (PreferredArchetypeVersion != Doc.ArchetypeVersion)
	{
		Metasound::Frontend::FMatchRootGraphToArchetype Transform(PreferredArchetypeVersion);
		if (Transform.Transform(GetDocumentHandle()))
		{
			MarkMetasoundDocumentDirty();
		}
	}
}

bool FMetasoundAssetBase::VersionAsset(bool bInMarkDirty)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FName AssetName;
	FString AssetPath;
	if (const UObject* OwningAsset = GetOwningAsset())
	{
		AssetName = FName(OwningAsset->GetName());
		AssetPath = OwningAsset->GetPathName();
	}

	FDocumentHandle DocumentHandle = GetDocumentHandle();

	const bool bDidUpdateDocumentVersion = FVersionDocument(AssetName, AssetPath).Transform(DocumentHandle);
	bool bDidMatchRootGraphToArchetype = false;
	if (FMetasoundFrontendDocument* Doc = GetDocument().Get())
	{
		FMetasoundFrontendVersion ArchetypeVerison = GetPreferredArchetypeVersion(*Doc);
		bDidMatchRootGraphToArchetype = FMatchRootGraphToArchetype(ArchetypeVerison).Transform(DocumentHandle);
	}

	const bool bDidUpdate = bDidUpdateDocumentVersion || bDidMatchRootGraphToArchetype;

	if (bInMarkDirty && bDidUpdate)
	{
		MarkMetasoundDocumentDirty();
	}

	return bDidUpdate;
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

void FMetasoundAssetBase::ConvertFromPreset()
{
	using namespace Metasound::Frontend;
	FGraphHandle GraphHandle = GetRootGraphHandle();
	FMetasoundFrontendGraphStyle Style = GraphHandle->GetGraphStyle();
	Style.bIsGraphEditable = true;
	GraphHandle->SetGraphStyle(Style);
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
	using namespace Metasound::Frontend;

	FConstDocumentHandle Document = GetDocumentHandle();

	// Find the archetype for the document.
	FMetasoundFrontendArchetype Archetype;
	FArchetypeRegistryKey ArchetypeRegistryKey = Frontend::GetArchetypeRegistryKey(Document->GetArchetypeVersion());
	bool bFoundArchetype = IArchetypeRegistry::Get().FindArchetype(ArchetypeRegistryKey, Archetype);

	if (!bFoundArchetype)
	{
		UE_LOG(LogMetaSound, Warning, TEXT("No registered archetype matching archetype version on document [ArchetypeVersion:%s]"), *Document->GetArchetypeVersion().ToString());
	}

	// Unused inputs are all input vertices that are not in the archetype.
	TArray<FString> ArchetypeInputVertexNames;
	for (const FMetasoundFrontendClassVertex& Vertex : Archetype.Interface.Inputs)
	{
		ArchetypeInputVertexNames.Add(Vertex.Name);
	}

	Frontend::FConstGraphHandle RootGraph = Document->GetRootGraph();
	TArray<FString> GraphInputVertexNames = RootGraph->GetInputVertexNames();

	// Filter graph inputs by archetype inputs.
	GraphInputVertexNames = GraphInputVertexNames.FilterByPredicate([&](const FString& InName) { return !ArchetypeInputVertexNames.Contains(InName); });

	auto IsDataTypeTransmittable = [&](const FString& InVertexName)
	{
		Frontend::FConstClassInputAccessPtr ClassInputPtr = RootGraph->FindClassInputWithName(InVertexName);
		if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
		{
			Frontend::FDataTypeRegistryInfo TypeInfo;
			if (Frontend::GetTraitsForDataType(ClassInput->TypeName, TypeInfo))
			{
				if (TypeInfo.bIsTransmittable)
				{
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

bool FMetasoundAssetBase::MarkMetasoundDocumentDirty() const
{
	if (const UObject* OwningAsset = GetOwningAsset())
	{
		return ensure(OwningAsset->MarkPackageDirty());
	}
	return false;
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

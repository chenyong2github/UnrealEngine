// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetBase.h"

#include "Algo/AnyOf.h"
#include "Algo/Transform.h"
#include "HAL/FileManager.h"
#include "Internationalization/Text.h"
#include "IStructSerializerBackend.h"
#include "Logging/LogMacros.h"
#include "MetasoundArchetype.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
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

	namespace AssetBasePrivate
	{
		void DepthFirstTraversal(const FMetasoundAssetBase& InInitAsset, TFunctionRef<TSet<const FMetasoundAssetBase*>(const FMetasoundAssetBase&)> InVisitFunction)
		{
			// Non recursive depth first traversal.
			TArray<const FMetasoundAssetBase*> Stack({ &InInitAsset });
			TSet<const FMetasoundAssetBase*> Visited;

			while (!Stack.IsEmpty())
			{
				const FMetasoundAssetBase* CurrentNode = Stack.Pop();
				if (!Visited.Contains(CurrentNode))
				{
					TArray<const FMetasoundAssetBase*> Children = InVisitFunction(*CurrentNode).Array();
					Stack.Append(Children);

					Visited.Add(CurrentNode);
				}
			}
		}
	} // namespace AssetBasePrivate
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
			FrontendClass.Metadata.SetType(EMetasoundFrontendClassType::External);
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

		virtual bool IsNative() const override
		{
			return false;
		}

	private:
		
		FString Name;
		FMetasoundFrontendDocument Document;
		FMetasoundFrontendClass FrontendClass;
		FNodeClassInfo ClassInfo;
	};

	UnregisterGraphWithFrontend();

	FNodeClassInfo AssetClassInfo = GetAssetClassInfo();
	if (const FMetasoundFrontendDocument* Doc = DocumentPtr.Get())
	{
		RegistryKey = FMetasoundFrontendRegistryContainer::Get()->RegisterNode(MakeUnique<FNodeRegistryEntry>(AssetName, *Doc, AssetClassInfo.AssetPath));
	}
	else
	{
		RegistryKey = FNodeRegistryKey();
	}

	if (NodeRegistryKey::IsValid(RegistryKey))
	{
#if WITH_EDITORONLY_DATA
		// Refresh Asset Registry Info if successfully registered with Frontend
		const FMetasoundFrontendGraphClass& DocumentClassGraph = GetDocumentHandle()->GetRootGraphClass();
		const FMetasoundFrontendClassMetadata& DocumentClassMetadata = DocumentClassGraph.Metadata;
		AssetClassInfo.AssetClassID = FGuid(DocumentClassMetadata.GetClassName().Name.ToString());
		FNodeClassName ClassName = DocumentClassMetadata.GetClassName().ToNodeClassName();
		FMetasoundFrontendClass GraphClass;
		ensure(ISearchEngine::Get().FindClassWithMajorVersion(ClassName, DocumentClassMetadata.GetVersion().Major, GraphClass));

		AssetClassInfo.Version = DocumentClassMetadata.GetVersion();

		AssetClassInfo.InputTypes.Reset();
		Algo::Transform(GraphClass.Interface.Inputs, AssetClassInfo.InputTypes, [] (const FMetasoundFrontendClassInput& Input) { return Input.TypeName; });

		AssetClassInfo.OutputTypes.Reset();
		Algo::Transform(GraphClass.Interface.Outputs, AssetClassInfo.OutputTypes, [](const FMetasoundFrontendClassOutput& Output) { return Output.TypeName; });

		SetRegistryAssetClassInfo(MoveTemp(AssetClassInfo));
#endif // WITH_EDITORONLY_DATA
	}
	else
	{
		UE_LOG(LogMetaSound, Error, TEXT("Failed to register node for MetaSoundSource [Name:%s]"), *AssetName);
	}
}

void FMetasoundAssetBase::UnregisterGraphWithFrontend()
{
	using namespace Metasound::Frontend;

	if (!NodeRegistryKey::IsValid(RegistryKey))
	{
		return;
	}

	const UObject* OwningAsset = GetOwningAsset();
	if (!ensureAlways(OwningAsset))
	{
		return;
	}

	ensureAlways(FMetasoundFrontendRegistryContainer::Get()->UnregisterNode(RegistryKey));
	RegistryKey = FNodeRegistryKey();
}

void FMetasoundAssetBase::SetMetadata(FMetasoundFrontendClassMetadata& InMetadata)
{
	FMetasoundFrontendDocument& Doc = GetDocumentChecked();
	Doc.RootGraph.Metadata = InMetadata;

	if (Doc.RootGraph.Metadata.GetType() != EMetasoundFrontendClassType::Graph)
	{
		UE_LOG(LogMetaSound, Display, TEXT("Forcing class type to EMetasoundFrontendClassType::Graph on root graph metadata"));
		Doc.RootGraph.Metadata.SetType(EMetasoundFrontendClassType::Graph);
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

bool FMetasoundAssetBase::AutoUpdate(const IMetaSoundAssetInterface& InAssetInterface, bool bInMarkDirty, bool bInUpdateReferencedAssets)
{
	using namespace Metasound::Frontend;

	bool bDidEdit = false;

	if (bInUpdateReferencedAssets)
	{
		const TSet<FSoftObjectPath>& ReferencedAssets = GetReferencedAssets();
		for (const FSoftObjectPath& ReferencedAsset : ReferencedAssets)
		{
			FMetasoundAssetBase* ReferencedMetaSound = InAssetInterface.TryLoadAsset(ReferencedAsset);
			if (ReferencedMetaSound)
			{
				bDidEdit |= FAutoUpdateRootGraph(*ReferencedMetaSound, InAssetInterface).Transform(ReferencedMetaSound->GetDocumentHandle());
			}
		}
	}

	bDidEdit |= FAutoUpdateRootGraph(*this, InAssetInterface).Transform(GetDocumentHandle());

	if (bDidEdit && bInMarkDirty)
	{
		MarkMetasoundDocumentDirty();
	}

	return bDidEdit;
}

bool FMetasoundAssetBase::VersionAsset(const IMetaSoundAssetInterface& InAssetInterface, bool bInMarkDirty, bool bInVersionReferencedAssets)
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

	bool bDidEdit = false;

	if (bInVersionReferencedAssets)
	{
		const TSet<FSoftObjectPath>& ReferencedAssets = GetReferencedAssets();
		for (const FSoftObjectPath& ReferencedAsset : ReferencedAssets)
		{
			FMetasoundAssetBase* ReferencedMetaSound = InAssetInterface.TryLoadAsset(ReferencedAsset);
			if (ensure(ReferencedMetaSound))
			{
				bDidEdit |= ReferencedMetaSound->VersionAsset(InAssetInterface, bInMarkDirty);
			}
		}
	}

	FDocumentHandle DocumentHandle = GetDocumentHandle();

	bDidEdit |= FVersionDocument(AssetName, AssetPath).Transform(DocumentHandle);
	if (FMetasoundFrontendDocument* Doc = GetDocument().Get())
	{
		FMetasoundFrontendVersion ArchetypeVerison = GetPreferredArchetypeVersion(*Doc);
		bDidEdit |= FMatchRootGraphToArchetype(ArchetypeVerison).Transform(DocumentHandle);
	}

	if (bDidEdit && bInMarkDirty)
	{
		MarkMetasoundDocumentDirty();
	}

	return bDidEdit;
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

void FMetasoundAssetBase::RebuildReferencedAssets(const IMetaSoundAssetInterface& InAssetInterface)
{
	using namespace Metasound::Frontend;

	TSet<FSoftObjectPath>& ReferencedAssets = GetReferencedAssets();
	ReferencedAssets.Reset();

	GetRootGraphHandle()->IterateConstNodes([AssetInterface = &InAssetInterface, RefAssets = &ReferencedAssets](FConstNodeHandle NodeHandle)
	{
		using namespace Metasound::Frontend;

		const FMetasoundFrontendClassMetadata& Metadata = NodeHandle->GetClassMetadata();
		const FNodeRegistryKey LocalRegistryKey = NodeRegistryKey::CreateKey(Metadata);
		if (const FSoftObjectPath* Path = AssetInterface->FindObjectPathFromKey(LocalRegistryKey))
		{
			if (Path->IsValid())
			{
				RefAssets->Add(*Path);
			}
		}
	}, EMetasoundFrontendClassType::External);
}

bool FMetasoundAssetBase::ContainReferenceLoop(const IMetaSoundAssetInterface& IMetaSoundAssetInterface) const
{
	return false;
}

bool FMetasoundAssetBase::AddingReferenceCausesLoop(const FSoftObjectPath& InReferencePath, const IMetaSoundAssetInterface& InAssetInterface) const
{
	const FMetasoundAssetBase* ReferenceAsset = InAssetInterface.TryLoadAsset(InReferencePath);
	if (!ensureAlways(ReferenceAsset))
	{
		return false;
	}

	bool bCausesLoop = false;
	const FMetasoundAssetBase* Parent = this;
	Metasound::AssetBasePrivate::DepthFirstTraversal(*ReferenceAsset, [&](const FMetasoundAssetBase& ChildAsset)
	{
		TSet<const FMetasoundAssetBase*> Children;

		if (Parent == &ChildAsset)
		{
			bCausesLoop = true;
			return Children;
		}

		for (const FSoftObjectPath& Path : ChildAsset.GetReferencedAssets())
		{
			const FMetasoundAssetBase* ChildReference = InAssetInterface.TryLoadAsset(Path);
			if (ensureAlways(ChildReference))
			{
				Children.Add(ChildReference);
			}
		}
		return Children;
	});

	return bCausesLoop;
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

	FMetasoundFrontendClassMetadata Metadata = GraphHandle->GetGraphMetadata();
	Metadata.SetAutoUpdateManagesInterface(false);
	GraphHandle->SetGraphMetadata(Metadata);
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
	FMetasoundFrontendDocument* Document = GetDocument().Get();
	if (ensure(nullptr != Document))
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
	Metasound::Frontend::FDocumentAccessPtr DocumentPtr = GetDocument();
	if (FMetasoundFrontendDocument* Document = DocumentPtr.Get())
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
	FMetasoundFrontendDocument* Document = GetDocument().Get();
	check(nullptr != Document);
	return *Document;
}

const FMetasoundFrontendDocument& FMetasoundAssetBase::GetDocumentChecked() const
{
	const FMetasoundFrontendDocument* Document = GetDocument().Get();

	check(nullptr != Document);
	return *Document;
}
#undef LOCTEXT_NAMESPACE // "MetaSound"

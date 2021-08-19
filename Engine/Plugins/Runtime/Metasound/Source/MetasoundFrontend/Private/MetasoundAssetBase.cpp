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
#include "MetasoundFrontendInjectReceiveNodes.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundInstanceTransmitter.h"
#include "MetasoundJsonBackend.h"
#include "MetasoundLog.h"
#include "MetasoundTrace.h"
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

IMetaSoundAssetManager* IMetaSoundAssetManager::Instance = nullptr;

void FMetasoundAssetBase::RegisterGraphWithFrontend(FMetaSoundAssetRegistrationOptions InRegistrationOptions)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(MetaSoundAssetBase::RegisterGraphWithFrontend);
	if (!InRegistrationOptions.bForceReregister)
	{
		if (IsRegistered())
		{
			return;
		}
	}

	GetReferencedAssetClassCache().Reset();

	if (InRegistrationOptions.bRebuildReferencedAssetClassKeys)
	{
		RebuildReferencedAssetClassKeys();
	}

	if (InRegistrationOptions.bRegisterDependencies)
	{
		const TArray<FMetasoundAssetBase*> References = FindOrLoadReferencedAssets();
		for (FMetasoundAssetBase* Reference : References)
		{
			if (InRegistrationOptions.bForceReregister || !Reference->IsRegistered())
			{
				// TODO: Check for infinite recursion and error if so
				Reference->RegisterGraphWithFrontend(InRegistrationOptions);
			}

			if (UObject* RefAsset = Reference->GetOwningAsset())
			{
				GetReferencedAssetClassCache().Add(RefAsset);
			}
		}
	}

	// Auto update must be done after all referenced asset classes are registered
	if (InRegistrationOptions.bAutoUpdate)
	{
		AutoUpdate();
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

	FString AssetName;
	FString AssetPath;
	const UObject* OwningAsset = GetOwningAsset();
	if (ensure(OwningAsset))
	{
		AssetName = OwningAsset->GetName();
		AssetPath = OwningAsset->GetPathName();
	}

	FNodeClassInfo AssetClassInfo = GetAssetClassInfo();
	const FMetasoundFrontendDocument* Doc = GetDocument().Get();
	if (Doc)
	{
		RegistryKey = FMetasoundFrontendRegistryContainer::Get()->RegisterNode(MakeUnique<FNodeRegistryEntry>(AssetName, *Doc, AssetClassInfo.AssetPath));
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
		FString ClassName;
		if (OwningAsset)
		{
			if (UClass* Class = OwningAsset->GetClass())
			{
				ClassName = Class->GetName();
			}
		}
		UE_LOG(LogMetaSound, Error, TEXT("Registration failed for MetaSound node class '%s' of UObject class '%s'"), *AssetName, *ClassName);
	}
}

void FMetasoundAssetBase::UnregisterGraphWithFrontend()
{
	using namespace Metasound::Frontend;
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(MetaSoundAssetBase::UnregisterGraphWithFrontend);

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

TArray<FMetasoundAssetBase*> FMetasoundAssetBase::FindOrLoadReferencedAssets() const
{
	using namespace Metasound::Frontend;

	TArray<FMetasoundAssetBase*> ReferencedAssets;
	for (const FNodeRegistryKey& Key : GetReferencedAssetClassKeys())
	{
		if (FMetasoundAssetBase* MetaSound = IMetaSoundAssetManager::GetChecked().FindAssetFromKey(Key))
		{
			ReferencedAssets.Add(MetaSound);
		}
		else
		{
			UE_LOG(LogMetaSound, Error, TEXT("Failed to find referenced MetaSound asset with key '%s'"), *Key);
		}
	}

	return ReferencedAssets;
}

bool FMetasoundAssetBase::GetArchetype(FMetasoundFrontendArchetype& OutArchetype) const
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	bool bFoundArchetype = false;

	const FMetasoundFrontendDocument* Document = GetDocument().Get();
	if (nullptr != Document)
	{
		FArchetypeRegistryKey ArchetypeRegistryKey = GetArchetypeRegistryKey(Document->ArchetypeVersion);
		bFoundArchetype = IArchetypeRegistry::Get().FindArchetype(ArchetypeRegistryKey, OutArchetype);

		if (!bFoundArchetype)
		{
			UE_LOG(LogMetaSound, Warning, TEXT("No registered archetype matching archetype version on document [ArchetypeVersion:%s]"), *Document->ArchetypeVersion.ToString());
		}
	}

	return bFoundArchetype;
}

void FMetasoundAssetBase::SetDocument(const FMetasoundFrontendDocument& InDocument)
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	Document = InDocument;
	MarkMetasoundDocumentDirty();
}

void FMetasoundAssetBase::ConformDocumentToArchetype()
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(MetaSoundAssetBase::ConformDocumentToArchetype);
	FMetasoundFrontendDocument& Doc = GetDocumentChecked();

	FMetasoundFrontendVersion PreferredArchetypeVersion = GetPreferredArchetypeVersion(Doc);

	if (PreferredArchetypeVersion != Doc.ArchetypeVersion)
	{
		Metasound::Frontend::FMatchRootGraphToArchetype Transform(PreferredArchetypeVersion);
		if (Transform.Transform(GetDocumentHandle()))
		{
			ConformObjectDataToArchetype();
			MarkMetasoundDocumentDirty();
		}
	}
}

bool FMetasoundAssetBase::AutoUpdate(bool bInMarkDirty)
{
	using namespace Metasound::Frontend;
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(MetaSoundAssetBase::AutoUpdate);

	const bool bUpdated = FAutoUpdateRootGraph().Transform(GetDocumentHandle());
	if (bUpdated && bInMarkDirty)
	{
		MarkMetasoundDocumentDirty();
	}

	return bUpdated;
}

bool FMetasoundAssetBase::VersionAsset()
{
	using namespace Metasound;
	using namespace Metasound::Frontend;
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(MetaSoundAssetBase::VersionAsset);

	FName AssetName;
	FString AssetPath;
	if (const UObject* OwningAsset = GetOwningAsset())
	{
		AssetName = FName(OwningAsset->GetName());
		AssetPath = OwningAsset->GetPathName();
	}

	bool bDidEdit = false;
	FDocumentHandle DocumentHandle = GetDocumentHandle();

	bDidEdit |= FVersionDocument(AssetName, AssetPath).Transform(DocumentHandle);
	if (FMetasoundFrontendDocument* Doc = GetDocument().Get())
	{
		FMetasoundFrontendVersion ArchetypeVersion = GetPreferredArchetypeVersion(*Doc);
		bDidEdit |= FMatchRootGraphToArchetype(ArchetypeVersion).Transform(DocumentHandle);
		if (bDidEdit)
		{
			ConformObjectDataToArchetype();
		}
	}

	return bDidEdit;
}

TUniquePtr<Metasound::IGraph> FMetasoundAssetBase::BuildMetasoundDocument() const
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(MetaSoundAssetBase::BuildMetasoundDocument);

	const FMetasoundFrontendDocument* Doc = GetDocument().Get();
	if (nullptr == Doc)
	{
		UE_LOG(LogMetaSound, Error, TEXT("Cannot create graph. Null MetaSound document in MetaSound asset [Name:%s]"), *GetOwningAssetName());
		return TUniquePtr<IGraph>(nullptr);
	}

	// Create graph which can spawn instances. TODO: cache graph.
	TUniquePtr<FFrontendGraph> FrontendGraph = FFrontendGraphBuilder::CreateGraph(*Doc);
	if (FrontendGraph.IsValid())
	{
		TSet<FVertexKey> VerticesToSkip = GetNonTransmittableInputVertices(*Doc);
		bool bSuccessfullyInjectedReceiveNodes = InjectReceiveNodes(*FrontendGraph, FMetasoundInstanceTransmitter::CreateSendAddressFromEnvironment, VerticesToSkip);
		if (!bSuccessfullyInjectedReceiveNodes)
		{
			UE_LOG(LogMetaSound, Error, TEXT("Error while injecting async communication hooks. Instance communication may not function properly [Name:%s]."), *GetOwningAssetName());
		}
	}

	return MoveTemp(FrontendGraph);
}

void FMetasoundAssetBase::RebuildReferencedAssetClassKeys()
{
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(MetaSoundAssetBase::RebuildReferencedAssetClassKeys);

	TSet<FNodeRegistryKey> AssetKeys;


	GetRootGraphHandle()->IterateConstNodes([RefAssetKeys = &AssetKeys](FConstNodeHandle NodeHandle)
	{
		using namespace Metasound::Frontend;

		const FMetasoundFrontendClassMetadata& RefMetadata = NodeHandle->GetClassMetadata();
		const FNodeRegistryKey RefRegistryKey = NodeRegistryKey::CreateKey(RefMetadata);
		if (const FMetasoundAssetBase* RefAsset = IMetaSoundAssetManager::GetChecked().FindAssetFromKey(RefRegistryKey))
		{
			RefAssetKeys->Add(RefRegistryKey);
		}
	}, EMetasoundFrontendClassType::External);

	SetReferencedAssetClassKeys(MoveTemp(AssetKeys));
}

bool FMetasoundAssetBase::IsRegistered() const
{
	using namespace Metasound::Frontend;

	if (!NodeRegistryKey::IsValid(RegistryKey))
	{
		return false;
	}

	return FMetasoundFrontendRegistryContainer::Get()->IsNodeRegistered(RegistryKey);
}

bool FMetasoundAssetBase::AddingReferenceCausesLoop(const FSoftObjectPath& InReferencePath) const
{
	const FMetasoundAssetBase* ReferenceAsset = IMetaSoundAssetManager::GetChecked().TryLoadAsset(InReferencePath);
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

		TArray<FMetasoundAssetBase*> ChildRefs = ChildAsset.FindOrLoadReferencedAssets();
		for (const FMetasoundAssetBase* ChildRef : ChildRefs)
		{
			if (ChildRef)
			{
				Children.Add(ChildRef);
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
	using namespace Metasound;
	using namespace Metasound::Frontend;
	using FSendInfo = FMetasoundInstanceTransmitter::FSendInfo;

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
			Info.SendInfo.Address = FMetasoundInstanceTransmitter::CreateSendAddressFromInstanceID(InInstanceID, InputHandle->GetName(), InputHandle->GetDataType());
			Info.SendInfo.ParameterName = FName(InputHandle->GetDisplayName().ToString()); // TODO: display name hack. Need to have naming consistent in editor for inputs
			//Info.SendInfo.ParameterName = FName(*InputHandle->GetName()); // TODO: this is the expected parameter name.
			Info.SendInfo.TypeName = InputHandle->GetDataType();
			Info.VertexName = VertexName;

			SendInfos.Add(Info);
		}
	}

	return SendInfos;
}

TSet<Metasound::FVertexKey> FMetasoundAssetBase::GetNonTransmittableInputVertices(const FMetasoundFrontendDocument& InDoc) const
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	check(IsInGameThread() || IsInAudioThread());

	TSet<FVertexKey> NonTransmittableInputVertices;

	auto GetVertexKey = [](const FMetasoundFrontendClassVertex& InVertex) { return InVertex.Name; };

	// Do not transmit vertices defined in archetype. Those are already accounted
	// for by owning object.
	FMetasoundFrontendArchetype Archetype;
	GetArchetype(Archetype);
	Algo::Transform(Archetype.Interface.Inputs, NonTransmittableInputVertices, GetVertexKey);


	// Do not transmit vertices which are not transmittable. Async communication 
	// is not supported without transmission.
	const IDataTypeRegistry& Registry = IDataTypeRegistry::Get();

	auto IsNotTransmittable = [&Registry](const FMetasoundFrontendClassVertex& InVertex) -> bool
	{	
		FDataTypeRegistryInfo Info;
		if (Registry.GetDataTypeInfo(InVertex.TypeName, Info))
		{
			return !Info.bIsTransmittable;
		}
		return true;
	};

	Algo::TransformIf(InDoc.RootGraph.Interface.Inputs, NonTransmittableInputVertices, IsNotTransmittable, GetVertexKey);

	return NonTransmittableInputVertices;
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
		using namespace Metasound::Frontend;

		Frontend::FConstClassInputAccessPtr ClassInputPtr = RootGraph->FindClassInputWithName(InVertexName);
		if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
		{
			Frontend::FDataTypeRegistryInfo TypeInfo;
			if (IDataTypeRegistry::Get().GetDataTypeInfo(ClassInput->TypeName, TypeInfo))
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
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(MetaSoundAssetBase::ImportFromJSON);

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
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(MetaSoundAssetBase::ImportFromJSONAsset);

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

FString FMetasoundAssetBase::GetOwningAssetName() const
{
	if (const UObject* OwningAsset = GetOwningAsset())
	{
		return OwningAsset->GetName();
	}
	return FString();
}

#undef LOCTEXT_NAMESPACE // "MetaSound"

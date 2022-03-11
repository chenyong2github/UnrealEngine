// Copyright Epic Games, Inc. All Rights Reserved.
#include "Metasound.h"

#include "AssetRegistryModule.h"
#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundEngineArchetypes.h"
#include "MetasoundEngineEnvironment.h"
#include "MetasoundEnvironment.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundGenerator.h"
#include "MetasoundLog.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundPrimitives.h"
#include "MetasoundReceiveNode.h"
#include "MetasoundTrigger.h"
#include "MetasoundUObjectRegistry.h"
#include "UObject/ObjectSaveContext.h"

#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#endif // WITH_EDITORONLY_DATA

#define LOCTEXT_NAMESPACE "MetaSound"


UMetaSound::UMetaSound(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FMetasoundAssetBase()
{
}

#if WITH_EDITOR
void UMetaSound::PostDuplicate(EDuplicateMode::Type InDuplicateMode)
{
	Super::PostDuplicate(InDuplicateMode);

	// Guid is reset as asset may share implementation from
	// asset duplicated from but should not be registered as such.
	if (InDuplicateMode == EDuplicateMode::Normal)
	{
		AssetClassID = FGuid::NewGuid();
		Metasound::Frontend::FRenameRootGraphClass::Generate(GetDocumentHandle(), AssetClassID);
	}
}

void UMetaSound::PostEditUndo()
{
	Super::PostEditUndo();
	Metasound::PostEditUndo(*this);
}
#endif // WITHEDITOR

void UMetaSound::BeginDestroy()
{
	UnregisterGraphWithFrontend();
	Super::BeginDestroy();
}

void UMetaSound::PreSave(FObjectPreSaveContext InSaveContext)
{
	Super::PreSave(InSaveContext);
	Metasound::PreSaveAsset(*this, InSaveContext);
}

void UMetaSound::Serialize(FArchive& InArchive)
{
	Super::Serialize(InArchive);
	Metasound::SerializeToArchive(*this, InArchive);
}

#if WITH_EDITORONLY_DATA
UEdGraph* UMetaSound::GetGraph()
{
	return Graph;
}

const UEdGraph* UMetaSound::GetGraph() const
{
	return Graph;
}

UEdGraph& UMetaSound::GetGraphChecked()
{
	check(Graph);
	return *Graph;
}

const UEdGraph& UMetaSound::GetGraphChecked() const
{
	check(Graph);
	return *Graph;
}

FText UMetaSound::GetDisplayName() const
{
	FString TypeName = UMetaSound::StaticClass()->GetName();
	return FMetasoundAssetBase::GetDisplayName(MoveTemp(TypeName));
}

void UMetaSound::SetRegistryAssetClassInfo(const Metasound::Frontend::FNodeClassInfo& InNodeInfo)
{
	Metasound::SetMetaSoundRegistryAssetClassInfo(*this, InNodeInfo);
}
#endif // WITH_EDITORONLY_DATA

Metasound::Frontend::FNodeClassInfo UMetaSound::GetAssetClassInfo() const
{
	return { GetDocumentChecked().RootGraph, *GetPathName() };
}

void UMetaSound::SetReferencedAssetClassKeys(TSet<Metasound::Frontend::FNodeRegistryKey>&& InKeys)
{
	ReferencedAssetClassKeys = MoveTemp(InKeys);
}

TSet<FSoftObjectPath>& UMetaSound::GetReferencedAssetClassCache()
{
	return ReferenceAssetClassCache;
}

const TSet<FSoftObjectPath>& UMetaSound::GetReferencedAssetClassCache() const
{
	return ReferenceAssetClassCache;
}
#undef LOCTEXT_NAMESPACE // MetaSound

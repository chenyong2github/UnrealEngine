// Copyright Epic Games, Inc. All Rights Reserved.
#include "Metasound.h"

#include "AssetRegistryModule.h"
#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundEngineEnvironment.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundGenerator.h"
#include "MetasoundInstanceTransmitter.h"
#include "MetasoundLog.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"
#include "MetasoundReceiveNode.h"
#include "MetasoundTrigger.h"
#include "MetasoundEnvironment.h"
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
void UMetaSound::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
	UpdateAssetTags(AssetTags);
	RegisterGraphWithFrontend();
}

void UMetaSound::PostEditUndo()
{
	Super::PostEditUndo();

	if (Graph)
	{
		Graph->Synchronize();
	}
}

void UMetaSound::PostEditChangeProperty(FPropertyChangedEvent& InEvent)
{
	Super::PostEditChangeProperty(InEvent);
}

#endif // WITHEDITOR

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
#endif // WITH_EDITORONLY_DATA

void UMetaSound::ConvertFromPreset()
{
	using namespace Metasound::Frontend;
	FGraphHandle GraphHandle = GetRootGraphHandle();
	FMetasoundFrontendGraphStyle Style = GraphHandle->GetGraphStyle();
	Style.bIsGraphEditable = true;
	GraphHandle->SetGraphStyle(Style);
}

const FMetasoundFrontendArchetype& UMetaSound::GetArchetype() const
{
	return GetBaseArchetype();
}

Metasound::FOperatorSettings UMetaSound::GetOperatorSettings(Metasound::FSampleRate InSampleRate) const
{
	const float BlockRate = FMath::Clamp(Metasound::ConsoleVariables::BlockRate, 1.0f, 1000.0f);
	return Metasound::FOperatorSettings(InSampleRate, BlockRate);
}

Metasound::FSendAddress UMetaSound::CreateSendAddress(uint64 InInstanceID, const FString& InVertexName, const FName& InDataTypeName) const
{
	using namespace Metasound;

	FSendAddress Address;

	Address.Subsystem = GetSubsystemNameForSendScope(ETransmissionScope::Global);
	Address.ChannelName = FName(FString::Printf(TEXT("%d:%s:%s"), InInstanceID, *InVertexName, *InDataTypeName.ToString()));

	return Address;
}

const TArray<FMetasoundFrontendArchetype>& UMetaSound::GetPreferredArchetypes() const
{
	static const TArray<FMetasoundFrontendArchetype> Preferred
	{
		GetBaseArchetype()
	};

	return Preferred;
}

const FString& UMetaSound::GetAudioDeviceHandleVariableName()
{
	static const FString AudioDeviceHandleVarName = TEXT("AudioDeviceHandle");
	return AudioDeviceHandleVarName;
}

const FMetasoundFrontendArchetype& UMetaSound::GetBaseArchetype()
{
	auto CreateBaseArchetype = []() -> FMetasoundFrontendArchetype
	{
		FMetasoundFrontendArchetype Archetype;
		
		FMetasoundFrontendEnvironmentVariable AudioDeviceHandle;
		AudioDeviceHandle.Name = GetAudioDeviceHandleVariableName();
		AudioDeviceHandle.Metadata.DisplayName = FText::FromString(AudioDeviceHandle.Name);
		AudioDeviceHandle.Metadata.Description = LOCTEXT("AudioDeviceHandleToolTip", "Audio device handle");

		Archetype.Interface.Environment.Add(AudioDeviceHandle);

		return Archetype;
	};

	static const FMetasoundFrontendArchetype BaseArchetype = CreateBaseArchetype();

	return BaseArchetype;
}
#undef LOCTEXT_NAMESPACE // MetaSound

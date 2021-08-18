// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundSource.h"

#include "AssetRegistryModule.h"
#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundEngineArchetypes.h"
#include "MetasoundEngineEnvironment.h"
#include "MetasoundEnvironment.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendInjectReceiveNodes.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundGenerator.h"
#include "MetasoundInstanceTransmitter.h"
#include "MetasoundLog.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"
#include "MetasoundReceiveNode.h"
#include "MetasoundTrace.h"
#include "MetasoundTrigger.h"
#include "MetasoundUObjectRegistry.h"
#include "UObject/ObjectSaveContext.h"

#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#endif // WITH_EDITORONLY_DATA

#define LOCTEXT_NAMESPACE "MetaSoundSource"

namespace MetaSoundSourcePrivate
{
	using FFormatOutputVertexKeyMap = TMap<EMetasoundSourceAudioFormat, TArray<Metasound::FVertexKey>>;

	const FFormatOutputVertexKeyMap& GetFormatOutputVertexKeys()
	{
		using namespace Metasound;
		using namespace Metasound::Engine;

		static const FFormatOutputVertexKeyMap Map
		(
			{
				{
					EMetasoundSourceAudioFormat::Mono, 
					{ MetasoundSourceMono::GetAudioOutputName() }
				},
				{
					EMetasoundSourceAudioFormat::Stereo, 
					{ 
						MetasoundSourceStereo::GetLeftAudioOutputName(), 
						MetasoundSourceStereo::GetRightAudioOutputName() 
					}
				}
			}
		);
		return Map;
	}
}


FAutoConsoleVariableRef CVarMetaSoundBlockRate(
	TEXT("au.MetaSound.BlockRate"),
	Metasound::ConsoleVariables::BlockRate,
	TEXT("Sets block rate (blocks per second) of MetaSounds.\n")
	TEXT("Default: 100.0f"),
	ECVF_Default);

UMetaSoundSource::UMetaSoundSource(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FMetasoundAssetBase()
{
	bRequiresStopFade = true;
	NumChannels = 1;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	
	// todo: ensure that we have a method so that the audio engine can be authoritative over the sample rate the UMetaSoundSource runs at.
	SampleRate = 48000.f;

}

#if WITH_EDITOR
void UMetaSoundSource::PostEditUndo()
{
	Super::PostEditUndo();
	Metasound::PostAssetUndo(*this);
}

void UMetaSoundSource::PostDuplicate(EDuplicateMode::Type InDuplicateMode)
{
	Super::PostDuplicate(InDuplicateMode);
	Metasound::PostDuplicate(*this, InDuplicateMode);
}

void UMetaSoundSource::PostEditChangeProperty(FPropertyChangedEvent& InEvent)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	Super::PostEditChangeProperty(InEvent);
	Metasound::PostEditChangeProperty(*this, InEvent);

	if (InEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMetaSoundSource, OutputFormat))
	{
		ConvertFromPreset();

		bool bDidModifyDocument = false;
		switch (OutputFormat)
		{
			case EMetasoundSourceAudioFormat::Mono:
			{
				// TODO: utilize latest version `MetasoudnSourceMono`
				bDidModifyDocument = Metasound::Frontend::FMatchRootGraphToArchetype(Metasound::Engine::MetasoundSourceMono::GetVersion()).Transform(GetDocumentHandle());
			}
			break;

			case EMetasoundSourceAudioFormat::Stereo:
			{
				bDidModifyDocument = Metasound::Frontend::FMatchRootGraphToArchetype(Metasound::Engine::MetasoundSourceStereo::GetVersion()).Transform(GetDocumentHandle());
			}
			break;

			default:
			{
				static_assert(static_cast<int32>(EMetasoundSourceAudioFormat::COUNT) == 2, "Possible missing format switch case coverage.");
			}
			break;
		}

		if (bDidModifyDocument)
		{
			ConformObjectDataToArchetype();

			// Use the editor form of register to ensure other editors'
			// MetaSounds are auto-updated if they are referencing this graph.
			if (Graph)
			{
				Graph->RegisterGraphWithFrontend();
			}
			MarkMetasoundDocumentDirty();
		}
	}
}
#endif // WITH_EDITOR

bool UMetaSoundSource::ConformObjectDataToArchetype()
{
	const FMetasoundFrontendVersion& ArchetypeVersion = GetDocumentHandle()->GetArchetypeVersion();
	if (ArchetypeVersion == Metasound::Engine::MetasoundSourceMono::GetVersion())
	{
		if (OutputFormat != EMetasoundSourceAudioFormat::Mono || NumChannels != 1)
		{
			OutputFormat = EMetasoundSourceAudioFormat::Mono;
			NumChannels = 1;
			return true;
		}
	}

	if (ArchetypeVersion == Metasound::Engine::MetasoundSourceStereo::GetVersion())
	{
		if (OutputFormat != EMetasoundSourceAudioFormat::Stereo || NumChannels != 2)
		{
			OutputFormat = EMetasoundSourceAudioFormat::Stereo;
			NumChannels = 2;
			return true;
		}
	}

	return false;
}


void UMetaSoundSource::BeginDestroy()
{
	UnregisterGraphWithFrontend();
	Super::BeginDestroy();
}

void UMetaSoundSource::PreSave(FObjectPreSaveContext InSaveContext)
{
	Super::PreSave(InSaveContext);
	Metasound::PreSaveAsset(*this);
}

void UMetaSoundSource::Serialize(FArchive& InArchive)
{
	Super::Serialize(InArchive);
	Metasound::SerializeToArchive(*this, InArchive);
}

void UMetaSoundSource::SetReferencedAssetClassKeys(TSet<Metasound::Frontend::FNodeRegistryKey>&& InKeys)
{
	ReferencedAssetClassKeys = MoveTemp(InKeys);
}

TSet<UObject*>& UMetaSoundSource::GetReferencedAssetClassCache()
{
	return ReferenceAssetClassCache;
}

#if WITH_EDITORONLY_DATA
UEdGraph* UMetaSoundSource::GetGraph()
{
	return Graph;
}

const UEdGraph* UMetaSoundSource::GetGraph() const
{
	return Graph;
}

UEdGraph& UMetaSoundSource::GetGraphChecked()
{
	check(Graph);
	return *Graph;
}

const UEdGraph& UMetaSoundSource::GetGraphChecked() const
{
	check(Graph);
	return *Graph;
}

FText UMetaSoundSource::GetDisplayName() const
{
	FString TypeName = UMetaSoundSource::StaticClass()->GetName();
	return FMetasoundAssetBase::GetDisplayName(MoveTemp(TypeName));
}

void UMetaSoundSource::SetRegistryAssetClassInfo(const Metasound::Frontend::FNodeClassInfo& InNodeInfo)
{
	Metasound::SetMetaSoundRegistryAssetClassInfo(*this, InNodeInfo);
}
#endif // WITH_EDITORONLY_DATA

void UMetaSoundSource::InitResources()
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundAssetSubsystem::AddOrUpdateAsset);

	FMetaSoundAssetRegistrationOptions RegOptions;
	RegOptions.bForceReregister = false;
	RegOptions.bRegisterDependencies = true;
	RegisterGraphWithFrontend(RegOptions);
}

Metasound::Frontend::FNodeClassInfo UMetaSoundSource::GetAssetClassInfo() const
{
	return { GetDocumentChecked().RootGraph, *GetPathName() };
}

bool UMetaSoundSource::IsPlayable() const
{
	// todo: cache off whether this metasound is buildable to an operator.
	return true;
}

bool UMetaSoundSource::SupportsSubtitles() const
{
	return Super::SupportsSubtitles();
}

const FMetasoundFrontendVersion& UMetaSoundSource::GetDefaultArchetypeVersion() const
{
	static const FMetasoundFrontendVersion DefaultVersion = Metasound::Engine::MetasoundSourceMono::GetVersion();

	return DefaultVersion;
}

float UMetaSoundSource::GetDuration()
{
	// eh? this is kind of a weird field anyways.
	return Super::GetDuration();
}

ISoundGeneratorPtr UMetaSoundSource::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;
	using namespace Metasound::Engine;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundSource::CreateSoundGenerator);

	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;

	SampleRate = InParams.SampleRate;
	FOperatorSettings InSettings = GetOperatorSettings(static_cast<FSampleRate>(SampleRate));
	FMetasoundEnvironment Environment = CreateEnvironment(InParams);

	// TODO: cache graph to avoid having to create it every call to `CreateSoundGenerator(...)`
	TUniquePtr<IGraph> MetasoundGraph = BuildMetasoundDocument();
	if (!MetasoundGraph.IsValid())
	{
		UE_LOG(LogMetaSound, Error, TEXT("Cannot create UMetaSoundSource SoundGenerator [Name:%s]. Failed to create MetaSound Graph"), *GetName());
		return ISoundGeneratorPtr(nullptr);
	}

	FMetasoundGeneratorInitParams InitParams = 
	{
		InSettings,
		MoveTemp(MetasoundGraph),
		Environment,
		GetName(),
		GetAudioOutputVertexKeys(),
		MetasoundSource::GetOnPlayInputName(),
		MetasoundSource::GetIsFinishedOutputName()
	};

	return ISoundGeneratorPtr(new FMetasoundGenerator(MoveTemp(InitParams)));
}

TUniquePtr<IAudioInstanceTransmitter> UMetaSoundSource::CreateInstanceTransmitter(const FAudioInstanceTransmitterInitParams& InParams) const
{
	Metasound::FMetasoundInstanceTransmitter::FInitParams InitParams(GetOperatorSettings(InParams.SampleRate), InParams.InstanceID);

	for (const FSendInfoAndVertexName& InfoAndName : FMetasoundAssetBase::GetSendInfos(InParams.InstanceID))
	{
		InitParams.Infos.Add(InfoAndName.SendInfo);
	}

	return MakeUnique<Metasound::FMetasoundInstanceTransmitter>(InitParams);
}

Metasound::FOperatorSettings UMetaSoundSource::GetOperatorSettings(Metasound::FSampleRate InSampleRate) const
{
	const float BlockRate = FMath::Clamp(Metasound::ConsoleVariables::BlockRate, 1.0f, 1000.0f);
	return Metasound::FOperatorSettings(InSampleRate, BlockRate);
}

const TArray<FMetasoundFrontendVersion>& UMetaSoundSource::GetSupportedArchetypeVersions() const 
{
	static const TArray<FMetasoundFrontendVersion> Supported(
	{
		Metasound::Engine::MetasoundSourceMono::GetVersion(),
		Metasound::Engine::MetasoundSourceStereo::GetVersion()
	});

	return Supported;
}


Metasound::FMetasoundEnvironment UMetaSoundSource::CreateEnvironment() const
{
	using namespace Metasound;
	using namespace Metasound::Engine;

	FMetasoundEnvironment Environment;
	// Add audio device ID to environment.
	FAudioDeviceHandle DeviceHandle;
	if (UWorld* World = GetWorld())
	{
		DeviceHandle = World->GetAudioDevice();
	}

	if (!DeviceHandle.IsValid())
	{
		if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
		{
			DeviceHandle = DeviceManager->GetMainAudioDeviceHandle();
		}
	}

	Environment.SetValue<FAudioDeviceHandle>(MetasoundSource::GetAudioDeviceHandleVariableName(), DeviceHandle);
	Environment.SetValue<uint32>(MetasoundSource::GetSoundUniqueIdName(), GetUniqueID());

	return Environment;
}

Metasound::FMetasoundEnvironment UMetaSoundSource::CreateEnvironment(const FSoundGeneratorInitParams& InParams) const
{
	using namespace Metasound;
	using namespace Metasound::Engine;

	FMetasoundEnvironment Environment = CreateEnvironment();
	Environment.SetValue<bool>(MetasoundSource::GetIsPreviewSoundName(), InParams.bIsPreviewSound);
	Environment.SetValue<uint64>(MetasoundSource::GetInstanceIDName(), InParams.InstanceID);
	
#if WITH_METASOUND_DEBUG_ENVIRONMENT
	Environment.SetValue<FString>(MetasoundSource::GetGraphName(), GetFullName());
#endif // WITH_METASOUND_DEBUG_ENVIRONMENT

	return Environment;
}

Metasound::FMetasoundEnvironment UMetaSoundSource::CreateEnvironment(const FAudioInstanceTransmitterInitParams& InParams) const
{
	using namespace Metasound;
	using namespace Metasound::Engine;

	FMetasoundEnvironment Environment = CreateEnvironment();
	Environment.SetValue<uint64>(MetasoundSource::GetInstanceIDName(), InParams.InstanceID);

	return Environment;
}

const TArray<Metasound::FVertexKey>& UMetaSoundSource::GetAudioOutputVertexKeys() const
{
	using namespace MetaSoundSourcePrivate;

	if (const TArray<Metasound::FVertexKey>* ArrayKeys = GetFormatOutputVertexKeys().Find(OutputFormat))
	{
		return *ArrayKeys;
	}
	else
	{
		// Unhandled audio format. Need to update audio output format vertex key map.
		checkNoEntry();
		static const TArray<Metasound::FVertexKey> Empty;
		return Empty;
	}
}

#undef LOCTEXT_NAMESPACE // MetaSoundSource

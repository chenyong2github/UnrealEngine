// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundSource.h"

#include "AssetRegistryModule.h"
#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundEngineEnvironment.h"
#include "MetasoundEnvironment.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundGenerator.h"
#include "MetasoundInstanceTransmitter.h"
#include "MetasoundLog.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"
#include "MetasoundReceiveNode.h"
#include "MetasoundTrigger.h"
#include "MetasoundUObjectRegistry.h"
#include "UObject/ObjectSaveContext.h"

#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#endif // WITH_EDITORONLY_DATA

#define LOCTEXT_NAMESPACE "MetaSoundSource"


FAutoConsoleVariableRef CVarMetaSoundBlockRate(
	TEXT("au.MetaSound.BlockRate"),
	Metasound::ConsoleVariables::BlockRate,
	TEXT("Sets block rate (blocks per second) of MetaSounds.\n")
	TEXT("Default: 100.0f"),
	ECVF_Default);

static const FName MetasoundSourceArchetypeName = "Metasound Source";

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
void UMetaSoundSource::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);

	// TODO: Move these to be run anytime the interface changes or
	// the node is versioned
	UpdateAssetTags(AssetTags);
	RegisterGraphWithFrontend();
	// TODO: Post re-register, determine if interface changed and
	// test/figure out when we should run updates on referencing
	// assets (presets only and on breaking changes? pop-up dialog?
	// Only done through an action user can run and just let other
	// be broken? Fail on cook if so?)
}

void UMetaSoundSource::PostEditUndo()
{
	Super::PostEditUndo();

	if (Graph)
	{
		Graph->Synchronize();
	}
}

void UMetaSoundSource::PostEditChangeProperty(FPropertyChangedEvent& InEvent)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	Super::PostEditChangeProperty(InEvent);

	if (InEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMetaSoundSource, OutputFormat))
	{
		ConvertFromPreset();

		switch (OutputFormat)
		{
			case EMetasoundSourceAudioFormat::Mono:
			{
				NumChannels = 1;
			}
			break;

			case EMetasoundSourceAudioFormat::Stereo:
			{
				NumChannels = 2;
			}
			break;

			default:
			{
				static_assert(static_cast<int32>(EMetasoundSourceAudioFormat::COUNT) == 2, "Possible missing format switch case coverage.");
			}
			break;
		}

		if (ensure(IsArchetypeSupported(GetArchetype())))
		{
			ConformDocumentToArchetype();
		}
	}
}
#endif // WITH_EDITOR

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
#endif // WITH_EDITORONLY_DATA

void UMetaSoundSource::ConvertFromPreset()
{
	using namespace Metasound::Frontend;
	FGraphHandle GraphHandle = GetRootGraphHandle();
	FMetasoundFrontendGraphStyle Style = GraphHandle->GetGraphStyle();
	Style.bIsGraphEditable = true;
	GraphHandle->SetGraphStyle(Style);
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

const FMetasoundFrontendArchetype& UMetaSoundSource::GetArchetype() const
{
	switch (OutputFormat)
	{
		case EMetasoundSourceAudioFormat::Mono:
		{
			return GetMonoSourceArchetype();
		}

		case EMetasoundSourceAudioFormat::Stereo:
		{
			return GetStereoSourceArchetype();
		}

		default:
		{
			static_assert(static_cast<int32>(EMetasoundSourceAudioFormat::COUNT) == 2, "Possible missing format switch case coverage.");
		}
	}

	return GetBaseArchetype();
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

	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;

	SampleRate = InParams.SampleRate;
	FOperatorSettings InSettings = GetOperatorSettings(static_cast<FSampleRate>(SampleRate));
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
	Environment.SetValue<FAudioDeviceHandle>(GetAudioDeviceHandleVariableName(), DeviceHandle);

	// Set the unique object ID as an environment variable
	Environment.SetValue<uint32>(GetSoundUniqueIdName(), GetUniqueID());
	Environment.SetValue<bool>(GetIsPreviewSoundName(), InParams.bIsPreviewSound);

	const FMetasoundFrontendDocument* OriginalDoc = GetDocument().Get();
	if (nullptr == OriginalDoc)
	{
		UE_LOG(LogMetaSound, Error, TEXT("Cannot create sound generator. Null Metasound document in UMetaSoundSource [Name:%s]"), *GetName());
		return ISoundGeneratorPtr(nullptr);
	}

	// Check version and attempt to version up if out-of-date
	Frontend::FDocumentHandle Document = GetDocumentHandle();
	FName AssetName = GetFName();
	FString AssetPath = GetPathName();
	if (Frontend::FVersionDocument(AssetName, AssetPath).Transform(Document))
	{
		const FMetasoundFrontendDocumentMetadata& Metadata = Document->GetMetadata();
		UE_LOG(LogMetaSound, Display,
			TEXT("Dynamically versioned UMetaSoundSource asset [Name:%s] to %s (Resave to avoid update at runtime)."),
			*GetName(),
			*Metadata.Version.Number.ToString());
	}

	// Inject receive nodes for unused transmittable inputs. Perform edits on a copy
	// of the document to avoid altering document.
	// TODO: Use a light wrapper of the document instead of a copy.
	FMetasoundFrontendDocument DocumentWithInjectedReceives;
	ensure(CopyDocumentAndInjectReceiveNodes(InParams.InstanceID, *OriginalDoc, DocumentWithInjectedReceives));

	// Create handles for new root graph
	FConstDocumentHandle NewDocumentHandle = IDocumentController::CreateDocumentHandle(MakeAccessPtr<FConstDocumentAccessPtr>(DocumentWithInjectedReceives.AccessPoint, DocumentWithInjectedReceives));

	FMetasoundGeneratorInitParams InitParams = 
	{
		InSettings,
		MoveTemp(DocumentWithInjectedReceives),
		Environment,
		NumChannels,
		GetName(),
		GetAudioOutputName(),
		GetOnPlayInputName(),
		GetIsFinishedOutputName()
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

const TArray<FMetasoundFrontendArchetype>& UMetaSoundSource::GetPreferredArchetypes() const 
{
	static const TArray<FMetasoundFrontendArchetype> Preferred({GetMonoSourceArchetype(), GetStereoSourceArchetype()});

	return Preferred;
}

const FString& UMetaSoundSource::GetOnPlayInputName()
{
	static const FString TriggerInputName = TEXT("On Play");
	return TriggerInputName;
}

const FString& UMetaSoundSource::GetAudioOutputName()
{
	static const FString AudioOutputName = TEXT("Generated Audio");
	return AudioOutputName;
}

const FString& UMetaSoundSource::GetIsFinishedOutputName()
{
	static const FString OnFinishedOutputName = TEXT("On Finished");
	return OnFinishedOutputName;
}

const FString& UMetaSoundSource::GetAudioDeviceHandleVariableName()
{
	static const FString AudioDeviceHandleVarName = TEXT("AudioDeviceHandle");
	return AudioDeviceHandleVarName;
}

const FString& UMetaSoundSource::GetSoundUniqueIdName()
{
	static const FString SoundUniqueIdVarName = TEXT("SoundUniqueId");
	return SoundUniqueIdVarName;
}

const FString& UMetaSoundSource::GetIsPreviewSoundName()
{
	static const FString SoundIsPreviewSoundName = TEXT("IsPreviewSound");
	return SoundIsPreviewSoundName;
}

const FMetasoundFrontendArchetype& UMetaSoundSource::GetBaseArchetype()
{
	auto CreateBaseArchetype = []() -> FMetasoundFrontendArchetype
	{
		FMetasoundFrontendArchetype Archetype;
		
		FMetasoundFrontendClassVertex OnPlayTrigger;
		OnPlayTrigger.Name = UMetaSoundSource::GetOnPlayInputName();

		OnPlayTrigger.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
		OnPlayTrigger.Metadata.DisplayName = LOCTEXT("OnPlay", "On Play");
		OnPlayTrigger.Metadata.Description = LOCTEXT("OnPlayTriggerToolTip", "Trigger executed when this source is played.");
		OnPlayTrigger.VertexID = FGuid::NewGuid();

		Archetype.Interface.Inputs.Add(OnPlayTrigger);

		FMetasoundFrontendClassVertex OnFinished;
		OnFinished.Name = UMetaSoundSource::GetIsFinishedOutputName();

		OnFinished.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
		OnFinished.Metadata.DisplayName = LOCTEXT("OnFinished", "On Finished");
		OnFinished.Metadata.Description = LOCTEXT("OnFinishedToolTip", "Trigger executed to initiate stopping the source.");
		OnFinished.VertexID = FGuid::NewGuid();

		Archetype.Interface.Outputs.Add(OnFinished);

		FMetasoundFrontendEnvironmentVariable AudioDeviceHandle;
		AudioDeviceHandle.Name = UMetaSoundSource::GetAudioDeviceHandleVariableName();
		AudioDeviceHandle.Metadata.DisplayName = FText::FromString(AudioDeviceHandle.Name);
		AudioDeviceHandle.Metadata.Description = LOCTEXT("AudioDeviceHandleToolTip", "Audio device handle");

		Archetype.Interface.Environment.Add(AudioDeviceHandle);

		return Archetype;
	};

	static const FMetasoundFrontendArchetype BaseArchetype = CreateBaseArchetype();

	return BaseArchetype;
}

const FMetasoundFrontendArchetype& UMetaSoundSource::GetMonoSourceArchetype()
{
	auto CreateMonoArchetype = []() -> FMetasoundFrontendArchetype
	{
		FMetasoundFrontendArchetype Archetype = GetBaseArchetype();
		Archetype.Name = "MonoSource";

		FMetasoundFrontendClassVertex GeneratedAudio;
		GeneratedAudio.Name = UMetaSoundSource::GetAudioOutputName();
		GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FMonoAudioFormat>();
		GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedMono", "Audio");
		GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
		GeneratedAudio.VertexID = FGuid::NewGuid();

		Archetype.Interface.Outputs.Add(GeneratedAudio);

		return Archetype;
	};

	static const FMetasoundFrontendArchetype MonoArchetype = CreateMonoArchetype();

	return MonoArchetype;
}

const FMetasoundFrontendArchetype& UMetaSoundSource::GetStereoSourceArchetype()
{
	auto CreateStereoArchetype = []() -> FMetasoundFrontendArchetype
	{
		FMetasoundFrontendArchetype Archetype = GetBaseArchetype();
		Archetype.Name = FName(TEXT("StereoSource"));

		FMetasoundFrontendClassVertex GeneratedAudio;
		GeneratedAudio.Name = UMetaSoundSource::GetAudioOutputName();
		GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FStereoAudioFormat>();
		GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedStereo", "Audio");
		GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
		GeneratedAudio.VertexID = FGuid::NewGuid();

		Archetype.Interface.Outputs.Add(GeneratedAudio);

		return Archetype;
	};

	static const FMetasoundFrontendArchetype StereoArchetype = CreateStereoArchetype();

	return StereoArchetype;
}
#undef LOCTEXT_NAMESPACE // MetaSoundSource

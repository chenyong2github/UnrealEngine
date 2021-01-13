// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundSource.h"

#include "AssetRegistryModule.h"
#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundBop.h"
#include "MetasoundEngineEnvironment.h"
#include "MetasoundGenerator.h"
#include "MetasoundLog.h"
#include "MetasoundPrimitives.h"


#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#endif // WITH_EDITORONLY_DATA

#define LOCTEXT_NAMESPACE "MetasoundSource"

static FName MetasoundSourceArchetypeName = FName(TEXT("Metasound Source"));

UMetasoundSource::UMetasoundSource(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FMetasoundAssetBase(GetMonoSourceArchetype())
{
	bRequiresStopFade = true;
	NumChannels = 1;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	
	// todo: ensure that we have a method so that the audio engine can be authoritative over the sample rate the UMetasoundSource runs at.
	SampleRate = 48000.0f;

}

void UMetasoundSource::SetMetadata(FMetasoundFrontendClassMetadata& InMetadata)
{
	RootMetasoundDocument.RootGraph.Metadata = InMetadata;
}

#if WITH_EDITOR

void UMetasoundSource::PostEditChangeProperty(FPropertyChangedEvent& InEvent)
{
	Super::PostEditChangeProperty(InEvent);

	if (InEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMetasoundSource, OutputFormat)) 
	{
		switch (OutputFormat)
		{
			case EMetasoundSourceAudioFormat::Stereo:

				NumChannels = 2;
				ensure(SetArchetype(GetStereoSourceArchetype()));

				break;

			case EMetasoundSourceAudioFormat::Mono:
			default:

				NumChannels = 1;
				ensure(SetArchetype(GetMonoSourceArchetype()));

				break;
		}
	}
}

#endif

bool UMetasoundSource::IsPlayable() const
{
	// todo: cache off whether this metasound is buildable to an operator.
	return true;
}

bool UMetasoundSource::SupportsSubtitles() const
{
	return Super::SupportsSubtitles();
}

float UMetasoundSource::GetDuration()
{
	// eh? this is kind of a weird field anyways.
	return Super::GetDuration();
}

ISoundGeneratorPtr UMetasoundSource::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	using namespace Metasound;

	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;

	SampleRate = InParams.SampleRate;
	const float BlockRate = 100.f; // Metasound graph gets evaluated 100 times per second.
	FOperatorSettings InSettings(InParams.SampleRate, BlockRate);
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

	// Create the operator from the graph handle.
	TArray<IOperatorBuilder::FBuildErrorPtr> BuildErrors;

	Frontend::FGraphHandle RootGraph = GetRootGraphHandle();
	ensureAlways(RootGraph->IsValid());

	TUniquePtr<IOperator> Operator = RootGraph->BuildOperator(InSettings, Environment, BuildErrors);

	if (!Operator.IsValid())
	{
		// Log build errors that resulted in a null operator.
		UE_LOG(LogMetasound, Error, TEXT("Failed to build Metasound operator from graph in MetasoundSource [%s]"), *GetName());
		for (const IOperatorBuilder::FBuildErrorPtr& Error : BuildErrors)
		{
			if (Error.IsValid())
			{
				UE_LOG(LogMetasound, Warning, TEXT("MetasoundSource [%s] build error [%s] \"%s\""), *GetName(), *(Error->GetErrorType().ToString()), *(Error->GetErrorDescription().ToString()));
			}
		}
	}
	else
	{
		FDataReferenceCollection Outputs = Operator->GetOutputs();
		TArrayView<FAudioBufferReadRef> OutputBuffers;

		// Get output audio buffers.
		if (EMetasoundSourceAudioFormat::Stereo == OutputFormat)
		{
			if (!Outputs.ContainsDataReadReference<FStereoAudioFormat>(GetAudioOutputName()))
			{
				UE_LOG(LogMetasound, Warning, TEXT("MetasoundSource [%s] does not contain stereo output [%s] in output"), *GetName(), *GetAudioOutputName());
			}
			OutputBuffers = Outputs.GetDataReadReferenceOrConstruct<FStereoAudioFormat>(GetAudioOutputName(), InSettings)->GetBuffers();
		}
		else if (EMetasoundSourceAudioFormat::Mono == OutputFormat)
		{
			if (!Outputs.ContainsDataReadReference<FMonoAudioFormat>(GetAudioOutputName()))
			{
				UE_LOG(LogMetasound, Warning, TEXT("MetasoundSource [%s] does not contain mono output [%s] in output"), *GetName(), *GetAudioOutputName());
			}
			OutputBuffers = Outputs.GetDataReadReferenceOrConstruct<FMonoAudioFormat>(GetAudioOutputName(), InSettings)->GetBuffers();
		}

		// References must be cached before moving the operator to the InitParams
		FDataReferenceCollection Inputs = Operator->GetInputs();
		FTriggerWriteRef PlayTrigger = Inputs.GetDataWriteReferenceOrConstruct<FBop>(GetOnPlayInputName(), InSettings, false);
		FTriggerReadRef FinishTrigger = Outputs.GetDataReadReferenceOrConstruct<FBop>(GetIsFinishedOutputName(), InSettings, false);

		// Create the FMetasoundGenerator.
		FMetasoundGeneratorInitParams InitParams =
		{
			MoveTemp(Operator),
			OutputBuffers,
			MoveTemp(PlayTrigger),
			MoveTemp(FinishTrigger)
		};

		return ISoundGeneratorPtr(new FMetasoundGenerator(MoveTemp(InitParams)));
	}

	return ISoundGeneratorPtr(nullptr);
}

void UMetasoundSource::PostLoad()
{
	Super::PostLoad();

	ConformDocumentToArchetype();
}

const TArray<FMetasoundFrontendArchetype>& UMetasoundSource::GetPreferredArchetypes() const 
{
	static const TArray<FMetasoundFrontendArchetype> Preferred({GetMonoSourceArchetype(), GetStereoSourceArchetype()});

	return Preferred;
}

const FString& UMetasoundSource::GetOnPlayInputName()
{
	static const FString TriggerInputName = TEXT("On Play");
	return TriggerInputName;
}

const FString& UMetasoundSource::GetAudioOutputName()
{
	static const FString AudioOutputName = TEXT("Generated Audio");
	return AudioOutputName;
}

const FString& UMetasoundSource::GetIsFinishedOutputName()
{
	static const FString OnFinishedOutputName = TEXT("On Finished");
	return OnFinishedOutputName;
}

const FString& UMetasoundSource::GetAudioDeviceHandleVariableName()
{
	static const FString AudioDeviceHandleVarName = TEXT("AudioDeviceHandle");
	return AudioDeviceHandleVarName;
}

const FMetasoundFrontendArchetype& UMetasoundSource::GetBaseArchetype()
{
	auto CreateBaseArchetype = []() -> FMetasoundFrontendArchetype
	{
		FMetasoundFrontendArchetype Archetype;
		
		FMetasoundFrontendClassVertex OnPlayTrigger;
		OnPlayTrigger.Name = UMetasoundSource::GetOnPlayInputName();
		OnPlayTrigger.Metadata.DisplayName = FText::FromString(OnPlayTrigger.Name);
		OnPlayTrigger.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FBop>();
		OnPlayTrigger.Metadata.Description = LOCTEXT("OnPlayTriggerToolTip", "Trigger executed when this source is played.");
		OnPlayTrigger.PointIDs.Add(2);

		Archetype.Interface.Inputs.Add(OnPlayTrigger);

		FMetasoundFrontendClassVertex OnFinished;
		OnFinished.Name = UMetasoundSource::GetIsFinishedOutputName();
		OnFinished.Metadata.DisplayName = FText::FromString(OnFinished.Name);
		OnFinished.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FBop>();
		OnFinished.Metadata.Description = LOCTEXT("OnFinishedToolTip", "Bop executed to initiate stopping the source.");
		OnFinished.PointIDs.Add(3);

		Archetype.Interface.Outputs.Add(OnFinished);

		FMetasoundFrontendEnvironmentVariable AudioDeviceHandle;
		AudioDeviceHandle.Name = UMetasoundSource::GetAudioDeviceHandleVariableName();
		AudioDeviceHandle.Metadata.DisplayName = FText::FromString(AudioDeviceHandle.Name);
		AudioDeviceHandle.Metadata.Description = LOCTEXT("AudioDeviceHandleToolTip", "Audio device handle");

		Archetype.Interface.Environment.Add(AudioDeviceHandle);

		return Archetype;
	};

	static const FMetasoundFrontendArchetype BaseArchetype = CreateBaseArchetype();

	return BaseArchetype;
}

const FMetasoundFrontendArchetype& UMetasoundSource::GetMonoSourceArchetype()
{
	auto CreateMonoArchetype = []() -> FMetasoundFrontendArchetype
	{
		FMetasoundFrontendArchetype Archetype = GetBaseArchetype();
		Archetype.Name = FName(TEXT("MonoSource"));

		FMetasoundFrontendClassVertex GeneratedAudio;
		GeneratedAudio.Name = UMetasoundSource::GetAudioOutputName();
		GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FMonoAudioFormat>();
		GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedMono", "Mono");
		GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
		GeneratedAudio.PointIDs.Add(4);

		Archetype.Interface.Outputs.Add(GeneratedAudio);

		return Archetype;
	};

	static const FMetasoundFrontendArchetype MonoArchetype = CreateMonoArchetype();

	return MonoArchetype;
}

const FMetasoundFrontendArchetype& UMetasoundSource::GetStereoSourceArchetype()
{
	auto CreateStereoArchetype = []() -> FMetasoundFrontendArchetype
	{
		FMetasoundFrontendArchetype Archetype = GetBaseArchetype();
		Archetype.Name = FName(TEXT("StereoSource"));

		FMetasoundFrontendClassVertex GeneratedAudio;
		GeneratedAudio.Name = UMetasoundSource::GetAudioOutputName();
		GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FStereoAudioFormat>();
		GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedStereo", "Stereo");
		GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
		GeneratedAudio.PointIDs.Add(5);

		Archetype.Interface.Outputs.Add(GeneratedAudio);

		return Archetype;
	};

	static const FMetasoundFrontendArchetype StereoArchetype = CreateStereoArchetype();

	return StereoArchetype;
}

#undef LOCTEXT_NAMESPACE

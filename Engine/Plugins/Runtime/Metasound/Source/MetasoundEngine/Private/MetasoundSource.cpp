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
	NumChannels = 1;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	
	// todo: ensure that we have a method so that the audio engine can be authoritative over the sample rate the UMetasoundSource runs at.
	SampleRate = 48000.0f;

}

void UMetasoundSource::SetMetadata(FMetasoundClassMetadata& InMetadata)
{
	RootMetasoundDocument.RootClass.Metadata = InMetadata;
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
	ensureAlways(RootGraph.IsValid());

	TUniquePtr<IOperator> Operator = RootGraph.BuildOperator(InSettings, Environment, BuildErrors);

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
			if (!Outputs.ContainsDataReadReference<FStereoAudioFormat>(GetAudioOutputName()))
			{
				UE_LOG(LogMetasound, Warning, TEXT("MetasoundSource [%s] does not contain mono output [%s] in output"), *GetName(), *GetAudioOutputName());
			}
			OutputBuffers = Outputs.GetDataReadReferenceOrConstruct<FMonoAudioFormat>(GetAudioOutputName(), InSettings)->GetBuffers();
		}

		// Create the FMetasoundGenerator.
		FMetasoundGeneratorInitParams InitParams =
		{
			MoveTemp(Operator),
			OutputBuffers,
			Outputs.GetDataReadReferenceOrConstruct<FBop>(GetIsFinishedOutputName(), InSettings, false)
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

const TArray<FMetasoundArchetype>& UMetasoundSource::GetPreferredArchetypes() const 
{
	static TArray<FMetasoundArchetype> Preferred({GetMonoSourceArchetype(), GetStereoSourceArchetype()});

	return Preferred;
}

const FString& UMetasoundSource::GetOnPlayInputName()
{
	static FString BopInputName = FString(TEXT("On Play"));
	return BopInputName;
}

const FString& UMetasoundSource::GetAudioOutputName()
{
	static FString AudioOutputName = FString(TEXT("Generated Audio"));
	return AudioOutputName;
}

const FString& UMetasoundSource::GetIsFinishedOutputName()
{
	static FString OnFinishedOutputName = FString(TEXT("On Finished"));
	return OnFinishedOutputName;
}

const FString& UMetasoundSource::GetAudioDeviceHandleVariableName()
{
	static FString AudioDeviceHandleVarName = FString(TEXT("AudioDeviceHandle"));
	return AudioDeviceHandleVarName;
}

const FMetasoundArchetype& UMetasoundSource::GetBaseArchetype()
{
	auto CreateBaseArchetype = []() -> FMetasoundArchetype
	{
		FMetasoundArchetype Archetype;
		
		FMetasoundInputDescription OnPlayBop;
		OnPlayBop.Name = UMetasoundSource::GetOnPlayInputName();
		OnPlayBop.DisplayName = FText::FromString(OnPlayBop.Name);
		OnPlayBop.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FBop>();
		OnPlayBop.ToolTip = LOCTEXT("OnPlayBopToolTip", "Bop executed when this source is first played.");

		Archetype.RequiredInputs.Add(OnPlayBop);

		FMetasoundOutputDescription OnFinished;
		OnFinished.Name = UMetasoundSource::GetIsFinishedOutputName();
		OnFinished.DisplayName = FText::FromString(OnFinished.Name);
		OnFinished.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FBop>();
		OnFinished.ToolTip = LOCTEXT("OnFinishedToolTip", "Bop executed to initiate stopping the source.");

		Archetype.RequiredOutputs.Add(OnFinished);

		FMetasoundEnvironmentVariableDescription AudioDeviceHandle;
		AudioDeviceHandle.Name = UMetasoundSource::GetAudioDeviceHandleVariableName();
		AudioDeviceHandle.DisplayName = FText::FromString(AudioDeviceHandle.Name);
		AudioDeviceHandle.ToolTip = LOCTEXT("AudioDeviceHandleToolTip", "Audio device handle");

		Archetype.EnvironmentVariables.Add(AudioDeviceHandle);

		return Archetype;
	};

	static const FMetasoundArchetype BaseArchetype = CreateBaseArchetype();

	return BaseArchetype;
}

const FMetasoundArchetype& UMetasoundSource::GetMonoSourceArchetype()
{
	auto CreateMonoArchetype = []() -> FMetasoundArchetype
	{
		FMetasoundArchetype Archetype = GetBaseArchetype();
		Archetype.ArchetypeName = FName(TEXT("MonoSource"));

		FMetasoundOutputDescription GeneratedAudio;
		GeneratedAudio.Name = UMetasoundSource::GetAudioOutputName();
		GeneratedAudio.DisplayName = LOCTEXT("GeneratedMono", "Mono");
		GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FMonoAudioFormat>();
		GeneratedAudio.ToolTip = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");

		Archetype.RequiredOutputs.Add(GeneratedAudio);

		return Archetype;
	};

	static const FMetasoundArchetype MonoArchetype = CreateMonoArchetype();

	return MonoArchetype;
}

const FMetasoundArchetype& UMetasoundSource::GetStereoSourceArchetype()
{
	auto CreateStereoArchetype = []() -> FMetasoundArchetype
	{
		FMetasoundArchetype Archetype = GetBaseArchetype();
		Archetype.ArchetypeName = FName(TEXT("StereoSource"));

		FMetasoundOutputDescription GeneratedAudio;
		GeneratedAudio.Name = UMetasoundSource::GetAudioOutputName();
		GeneratedAudio.DisplayName = LOCTEXT("GeneratedStereo", "Stereo");
		GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FStereoAudioFormat>();
		GeneratedAudio.ToolTip = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");

		Archetype.RequiredOutputs.Add(GeneratedAudio);

		return Archetype;
	};

	static const FMetasoundArchetype StereoArchetype = CreateStereoArchetype();

	return StereoArchetype;
}

#undef LOCTEXT_NAMESPACE

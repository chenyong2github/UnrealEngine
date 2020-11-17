// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundSource.h"

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "AssetRegistryModule.h"

#include "MetasoundAssetBase.h"
#include "MetasoundBop.h"
#include "MetasoundEngineEnvironment.h"
#include "MetasoundGenerator.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundPrimitives.h"

#include "MetasoundArchetypeRegistration.h"

#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#endif // WITH_EDITORONLY_DATA

#define LOCTEXT_NAMESPACE "MetasoundSource"

static FName MetasoundSourceArchetypeName = FName(TEXT("Metasound Source"));

UMetasoundSource::UMetasoundSource(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FMetasoundAssetBase(RootMetasoundDocument)
{
	// TODO: 
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

	NumChannels = 1;
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

	TArray<IOperatorBuilder::FBuildErrorPtr> BuildErrors;

	Frontend::FGraphHandle RootGraph = GetRootGraphHandle();
	ensureAlways(RootGraph.IsValid());

	TUniquePtr<IOperator> Operator = RootGraph.BuildOperator(InSettings, Environment, BuildErrors);
	if (ensureAlways(Operator.IsValid()))
	{
		FDataReferenceCollection Outputs = Operator->GetOutputs();

		FMetasoundGeneratorInitParams InitParams =
		{
			MoveTemp(Operator),
			Outputs.GetDataReadReferenceOrConstruct<FAudioBuffer>(GetAudioOutputName(), InSettings.GetNumFramesPerBlock()),
			Outputs.GetDataReadReferenceOrConstruct<FBop>(GetIsFinishedOutputName(), InSettings, false)
		};

		return ISoundGeneratorPtr(new FMetasoundGenerator(MoveTemp(InitParams)));
	}
	else
	{
		return ISoundGeneratorPtr(nullptr);
	}
}

void UMetasoundSource::PostLoad()
{
	Super::PostLoad();

	ConformDocumentToArchetype();
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

FMetasoundArchetype UMetasoundSource::GetArchetype() const
{
	FMetasoundArchetype Archetype;
	Archetype.ArchetypeName = MetasoundSourceArchetypeName;

	FMetasoundInputDescription OnPlayBop;
	OnPlayBop.Name = GetOnPlayInputName();
	OnPlayBop.DisplayName = FText::FromString(OnPlayBop.Name);
	OnPlayBop.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FBop>();
	OnPlayBop.ToolTip = LOCTEXT("OnPlayBopToolTip", "Bop executed when this source is first played.");

	Archetype.RequiredInputs.Add(OnPlayBop);

	FMetasoundOutputDescription GeneratedAudio;
	GeneratedAudio.Name = GetAudioOutputName();
	GeneratedAudio.DisplayName = FText::FromString(GeneratedAudio.Name);
	GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FAudioBuffer>();
	GeneratedAudio.ToolTip = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
	Archetype.RequiredOutputs.Add(GeneratedAudio);

	FMetasoundOutputDescription OnFinished;
	OnFinished.Name = GetIsFinishedOutputName();
	OnFinished.DisplayName = FText::FromString(OnFinished.Name);
	OnFinished.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FBop>();
	OnFinished.ToolTip = LOCTEXT("OnFinishedToolTip", "Bop executed to initiate stopping the source.");
	Archetype.RequiredOutputs.Add(OnFinished);

	FMetasoundEnvironmentVariableDescription AudioDeviceHandle;
	AudioDeviceHandle.Name = GetAudioDeviceHandleVariableName();
	AudioDeviceHandle.DisplayName = FText::FromString(AudioDeviceHandle.Name);
	AudioDeviceHandle.ToolTip = LOCTEXT("AudioDeviceHandleToolTip", "Audio device handle");
	Archetype.EnvironmentVariables.Add(AudioDeviceHandle);

	return Archetype;
}

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundSource.h"

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "AssetRegistryModule.h"

#include "MetasoundAssetBase.h"
#include "MetasoundBop.h"
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
	NumChannels = 1;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	SampleRate = InParams.SampleRate;
	const float BlockRate = 100.f; // Metasound graph gets evaluated 100 times per second.

	Metasound::FOperatorSettings InSettings(InParams.SampleRate, BlockRate);

	TArray<Metasound::IOperatorBuilder::FBuildErrorPtr> BuildErrors;

	Metasound::Frontend::FGraphHandle RootGraph = GetRootGraphHandle();
	ensureAlways(RootGraph.IsValid());

	TUniquePtr<Metasound::IOperator> Operator = RootGraph.BuildOperator(InSettings, BuildErrors);
	if (ensureAlways(Operator.IsValid()))
	{
		Metasound::FDataReferenceCollection Outputs = Operator->GetOutputs();

		Metasound::FMetasoundGeneratorInitParams InitParams =
		{
			MoveTemp(Operator),
			Outputs.GetDataReadReferenceOrConstruct<Metasound::FAudioBuffer>(GetAudioOutputName(), InSettings.GetNumFramesPerBlock()),
			Outputs.GetDataReadReferenceOrConstruct<Metasound::FBop>(GetIsFinishedOutputName(), false, InSettings)
		};

		return ISoundGeneratorPtr(new Metasound::FMetasoundGenerator(MoveTemp(InitParams)));
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

	return Archetype;
}

#undef LOCTEXT_NAMESPACE

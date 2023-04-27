// Copyright Epic Games, Inc. All Rights Reserved.

#include "TakeRecorderMicrophoneAudioSource.h"
#include "TakeRecorderMicrophoneAudioManager.h"
#include "TakeRecorderSources.h"
#include "TakeRecorderSettings.h"
#include "TakesUtils.h"
#include "TakeMetaData.h"

#include "AudioCaptureEditorTypes.h"
#include "Editor.h"
#include "LevelSequence.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneFolder.h"
#include "Recorder/TakeRecorderParameters.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Sound/SoundWave.h"
#include "Styling/SlateIconFinder.h"
#include "Tracks/MovieSceneAudioTrack.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "ObjectEditorUtils.h"
#include "ObjectTools.h"
#include "UObject/UObjectBaseUtility.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TakeRecorderMicrophoneAudioSource)

UTakeRecorderMicrophoneAudioSourceSettings::UTakeRecorderMicrophoneAudioSourceSettings(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
	, AudioTrackName(NSLOCTEXT("UTakeRecorderMicrophoneAudioSource", "DefaultAudioTrackName", "Recorded Audio"))
	, AudioAssetName(TEXT("Audio_{slate}_{take}"))
	, AudioSubDirectory(TEXT("Audio"))
{
	TrackTint = FColor(75, 67, 148);
}

void UTakeRecorderMicrophoneAudioSourceSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		SaveConfig();
	}
}

FString UTakeRecorderMicrophoneAudioSourceSettings::GetSubsceneTrackName(ULevelSequence* InSequence) const
{
	if (UTakeMetaData* TakeMetaData = InSequence->FindMetaData<UTakeMetaData>())
	{
		return FString::Printf(TEXT("%s_%s"), *AudioTrackName.ToString(), *TakeMetaData->GenerateAssetPath("{slate}"));
	}
	return TEXT("MicrophoneAudio");
}

FString UTakeRecorderMicrophoneAudioSourceSettings::GetSubsceneAssetName(ULevelSequence* InSequence) const
{
	if (UTakeMetaData* TakeMetaData = InSequence->FindMetaData<UTakeMetaData>())
	{
		return TakeMetaData->GenerateAssetPath(AudioAssetName);
	}
	return TEXT("MicrophoneAudio");
}

UTakeRecorderMicrophoneAudioSource::UTakeRecorderMicrophoneAudioSource(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
	, AudioGain(0.0f)
	, bReplaceRecordedAudio(true)
{
}

void UTakeRecorderMicrophoneAudioSource::Initialize()
{
	UTakeRecorderMicrophoneAudioManager* AudioInputManager = GetAudioInputManager();
	if (AudioInputManager != nullptr)
	{
		TUniquePtr<TArray<bool>> ChannelsInUse = GetChannelsInUse(AudioInputManager->GetDeviceChannelCount());
		int32 AvailableChannelNumber = INDEX_NONE;

		for (int32 ChannelIndex = 0; ChannelIndex < ChannelsInUse->Num(); ++ChannelIndex)
		{
			if (!(*ChannelsInUse)[ChannelIndex])
			{
				AvailableChannelNumber = ChannelIndex + 1;
				break;
			}
		}

		if (AvailableChannelNumber != INDEX_NONE)
		{
			SetCurrentInputChannel(AvailableChannelNumber);
		}

		AudioInputManager->GetOnNotifySourcesOfDeviceChange().AddUObject(this, &UTakeRecorderMicrophoneAudioSource::OnNotifySourcesOfDeviceChange);
	}
}

void UTakeRecorderMicrophoneAudioSource::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

UTakeRecorderMicrophoneAudioManager* UTakeRecorderMicrophoneAudioSource::GetAudioInputManager()
{
	return GetMutableDefault<UTakeRecorderMicrophoneAudioManager>();
}

void UTakeRecorderMicrophoneAudioSource::OnNotifySourcesOfDeviceChange(int32 InChannelCount)
{
	SetAudioDeviceChannelCount(InChannelCount);
}

TUniquePtr<TArray<bool>> UTakeRecorderMicrophoneAudioSource::GetChannelsInUse(const int32 InDeviceChannelCount)
{
	TUniquePtr<TArray<bool>> ChannelsInUse = MakeUnique<TArray<bool>>();
	ChannelsInUse->Init(false, InDeviceChannelCount);

	UTakeRecorderSources* SourcesList = GetTypedOuter<UTakeRecorderSources>();
	for (UTakeRecorderSource* Source : SourcesList->GetSources())
	{
		if (UTakeRecorderMicrophoneAudioSource* MicSource = Cast<UTakeRecorderMicrophoneAudioSource>(Source))
		{
			int32 MicSourceChannelNumber = MicSource->AudioChannel.AudioInputDeviceChannel;
			int32 ChannelIndex = MicSourceChannelNumber - 1;
			if (ChannelIndex >= 0 && ChannelIndex < InDeviceChannelCount)
			{
				(*ChannelsInUse)[ChannelIndex] = true;
			}
		}
	}

	return ChannelsInUse;
}

void UTakeRecorderMicrophoneAudioSource::SetAudioDeviceChannelCount(int32 InChannelCount)
{
	if (ensure(InChannelCount >= 0))
	{
		if (AudioChannel.AudioInputDeviceChannel > InChannelCount)
		{
			AudioChannel.AudioInputDeviceChannel = 0;
		}
	}
}

static FString MakeNewAssetName(const FString& BaseAssetPath, const FString& BaseAssetName)
{
	const FString Dot(TEXT("."));
	FString AssetPath = BaseAssetPath;
	FString AssetName = BaseAssetName;

	AssetPath /= AssetName;
	AssetPath += Dot + AssetName;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));

	// if object with same name exists, try a different name until we don't find one
	int32 ExtensionIndex = 0;
	while (AssetData.IsValid())
	{
		AssetName = FString::Printf(TEXT("%s_%d"), *BaseAssetName, ExtensionIndex);
		AssetPath = (BaseAssetPath / AssetName) + Dot + AssetName;
		AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));

		ExtensionIndex++;
	}

	return AssetName;
}

TArray<UTakeRecorderSource*> UTakeRecorderMicrophoneAudioSource::PreRecording(ULevelSequence* InSequence, FMovieSceneSequenceID InSequenceID, ULevelSequence* InRootSequence, FManifestSerializer* InManifestSerializer)
{
	if (AudioChannel.AudioInputDeviceChannel > 0)
	{
		UMovieScene* MovieScene = InSequence->GetMovieScene();
		for (auto Track : MovieScene->GetTracks())
		{
			if (Track->IsA(UMovieSceneAudioTrack::StaticClass()) && Track->GetDisplayName().EqualTo(AudioTrackName))
			{
				CachedAudioTrack = Cast<UMovieSceneAudioTrack>(Track);
			}
		}

		if (!CachedAudioTrack.IsValid())
		{
			CachedAudioTrack = MovieScene->AddTrack<UMovieSceneAudioTrack>();
			CachedAudioTrack->SetDisplayName(AudioTrackName);
		}

		FString PathToRecordTo = FPackageName::GetLongPackagePath(InSequence->GetOutermost()->GetPathName());
		FString BaseName = InSequence->GetName();

		AudioDirectory.Path = PathToRecordTo;
		if (AudioSubDirectory.Len())
		{
			AudioDirectory.Path /= AudioSubDirectory;
		}

		AssetName = MakeNewAssetName(AudioDirectory.Path, BaseName);
	}

	RecordedSoundWaves.Empty();

	return TArray<UTakeRecorderSource*>();
}

void UTakeRecorderMicrophoneAudioSource::AddContentsToFolder(UMovieSceneFolder* InFolder)
{
	if (CachedAudioTrack.IsValid())
	{
		InFolder->AddChildTrack(CachedAudioTrack.Get());
	}
}


void UTakeRecorderMicrophoneAudioSource::StartRecording(const FTimecode& InSectionStartTimecode, const FFrameNumber& InSectionFirstFrame, class ULevelSequence* InSequence)
{
	Super::StartRecording(InSectionStartTimecode, InSectionFirstFrame, InSequence);

	StartTimecode = InSectionStartTimecode;

	UTakeRecorderMicrophoneAudioManager* AudioInputManager = GetAudioInputManager();
	if (AudioInputManager != nullptr)
	{
		int32 LastChannelInUse = INDEX_NONE;
		TUniquePtr<TArray<bool>> ChannelsInUse = GetChannelsInUse(AudioInputManager->GetDeviceChannelCount());

		for (int32 ChannelIndex = 0; ChannelIndex < ChannelsInUse->Num(); ++ChannelIndex)
		{
			if ((*ChannelsInUse)[ChannelIndex])
			{
				LastChannelInUse = FMath::Max(LastChannelInUse, ChannelIndex + 1);
			}
		}

		AudioInputManager->StartRecording(LastChannelInUse);
	}
}

void UTakeRecorderMicrophoneAudioSource::StopRecording(class ULevelSequence* InSequence)
{
	Super::StopRecording(InSequence);

	UTakeRecorderMicrophoneAudioManager* AudioInputManager = GetAudioInputManager();
	if (AudioInputManager != nullptr)
	{
		AudioInputManager->StopRecording();
	}
}

TArray<UTakeRecorderSource*> UTakeRecorderMicrophoneAudioSource::PostRecording(ULevelSequence* InSequence, class ULevelSequence* InRootSequence, const bool bCancelled)
{
	GetRecordedSoundWave(InSequence);

	if (!RecordedSoundWaves.Num())
	{
		return TArray<UTakeRecorderSource*>();
	}

	TArray<UObject*> AssetsToCleanUp;
	if (bCancelled)
	{
		for (TWeakObjectPtr<USoundWave> WeakRecordedSoundWave : RecordedSoundWaves)
		{
			if (USoundWave* RecordedSoundWave = WeakRecordedSoundWave.Get())
			{
				AssetsToCleanUp.Add(RecordedSoundWave);
			}
		}
	}
	else
	{
		for (TWeakObjectPtr<USoundWave> WeakRecordedSoundWave : RecordedSoundWaves)
		{
			if (USoundWave* RecordedSoundWave = WeakRecordedSoundWave.Get())
			{
				RecordedSoundWave->MarkPackageDirty();
		
				FAssetRegistryModule::AssetCreated(RecordedSoundWave);
			}
		}

		UMovieScene* MovieScene = InSequence->GetMovieScene();
		check(CachedAudioTrack.IsValid());

		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();

		if (bReplaceRecordedAudio)
		{
			CachedAudioTrack->RemoveAllAnimationData();
		}

		UTakeRecorderSources* Sources = GetTypedOuter<UTakeRecorderSources>();

		for (TWeakObjectPtr<USoundWave> WeakRecordedSoundWave : RecordedSoundWaves)
		{
			if (USoundWave* RecordedSoundWave = WeakRecordedSoundWave.Get())
			{
				int32 RowIndex = -1;
				for (UMovieSceneSection* Section : CachedAudioTrack->GetAllSections())
				{
					RowIndex = FMath::Max(RowIndex, Section->GetRowIndex());
				}

				UMovieSceneAudioSection* NewAudioSection = NewObject<UMovieSceneAudioSection>(CachedAudioTrack.Get(), UMovieSceneAudioSection::StaticClass());

				FFrameNumber RecordStartFrame = MovieScene->GetPlaybackRange().GetLowerBoundValue();

				NewAudioSection->SetRowIndex(RowIndex + 1);
				NewAudioSection->SetSound(RecordedSoundWave);
				NewAudioSection->SetRange(TRange<FFrameNumber>(RecordStartFrame, RecordStartFrame + (RecordedSoundWave->GetDuration() * TickResolution).CeilToFrame()));
				NewAudioSection->TimecodeSource = FTimecode::FromFrameNumber(RecordStartFrame, DisplayRate);

				CachedAudioTrack->AddSection(*NewAudioSection);

				if ((Sources && Sources->GetSettings().bSaveRecordedAssets) || GEditor == nullptr)
				{
					TakesUtils::SaveAsset(RecordedSoundWave);
				}
			}
		}
	}

	// Reset our audio track pointer
	CachedAudioTrack = nullptr;
	RecordedSoundWaves.Empty();
	
	if (GEditor && AssetsToCleanUp.Num() > 0)
	{
		ObjectTools::ForceDeleteObjects(AssetsToCleanUp, false);
	}

	return TArray<UTakeRecorderSource*>();
}

void UTakeRecorderMicrophoneAudioSource::FinalizeRecording()
{
	UTakeRecorderMicrophoneAudioManager* AudioInputManager = GetAudioInputManager();
	if (AudioInputManager != nullptr)
	{
		AudioInputManager->FinalizeRecording();
	}
}

void UTakeRecorderMicrophoneAudioSource::GetRecordedSoundWave(ULevelSequence* InSequence)
{
	UTakeRecorderMicrophoneAudioManager* AudioInputManager = GetAudioInputManager();
	if (AudioChannel.AudioInputDeviceChannel > 0 && AudioInputManager != nullptr)
	{
		UMovieScene* MovieScene = InSequence->GetMovieScene();
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();

		FTakeRecorderAudioSourceSettings AudioSettings;
		AudioSettings.Directory = AudioDirectory;
		AudioSettings.AssetName = AssetName;
		AudioSettings.GainDb = AudioGain;
		AudioSettings.InputChannelNumber = AudioChannel.AudioInputDeviceChannel;
		AudioSettings.StartTimecode = StartTimecode;
		AudioSettings.VideoFrameRate = DisplayRate;

		TObjectPtr<USoundWave> SoundWave = AudioInputManager->GetRecordedSoundWave(AudioSettings);
		if (SoundWave != nullptr)
		{
			FSoundWaveTimecodeInfo TimecodeInfo;
			FSoundTimecodeOffset TimecodeOffset;

			TimecodeOffset.NumOfSecondsSinceMidnight = StartTimecode.ToTimespan(DisplayRate).GetTotalSeconds();
			SoundWave->SetTimecodeOffset(TimecodeOffset);
			
			RecordedSoundWaves.Add(SoundWave);
		}
	}
}

FText UTakeRecorderMicrophoneAudioSource::GetDisplayTextImpl() const
{
	return NSLOCTEXT("UTakeRecorderMicrophoneAudioSource", "Label", "Microphone Audio");
}

bool UTakeRecorderMicrophoneAudioSource::CanAddSource(UTakeRecorderSources* InSources) const
{
	int32 MicrophoneSourceCount = 0;
	for (UTakeRecorderSource* Source : InSources->GetSources())
	{
		if (Source->IsA<UTakeRecorderMicrophoneAudioSource>())
		{
			++MicrophoneSourceCount;
		}
	}
	return MicrophoneSourceCount < FAudioInputDeviceProperty::MaxInputChannelCount;
}

void UTakeRecorderMicrophoneAudioSource::SetCurrentInputChannel(int32 InChannelNumber)
{
	if (ensure(InChannelNumber >= 0 && InChannelNumber <= FAudioInputDeviceProperty::MaxInputChannelCount))
	{
		AudioChannel.AudioInputDeviceChannel = InChannelNumber;
	}
}

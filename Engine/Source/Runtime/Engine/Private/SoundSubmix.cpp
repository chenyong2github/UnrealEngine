// Copyright Epic Games, Inc. All Rights Reserved.
#include "Sound/SoundSubmix.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "Sound/SoundSubmixSend.h"
#include "UObject/UObjectIterator.h"
#include "DSP/Dsp.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif // WITH_EDITOR

static int32 ClearBrokenSubmixAssetsCVar = 0;
FAutoConsoleVariableRef CVarFixUpBrokenSubmixAssets(
	TEXT("au.submix.clearbrokensubmixassets"),
	ClearBrokenSubmixAssetsCVar,
	TEXT("If fixed, will verify that we don't have a submix list a child submix that doesn't have it as it's parent, or vice versa.\n")
	TEXT("0: Disable, >0: Enable"),
	ECVF_Default);

USoundSubmixWithParentBase::USoundSubmixWithParentBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ParentSubmix(nullptr)
{}

USoundSubmixBase::USoundSubmixBase(const FObjectInitializer& ObjectInitializer)
#if WITH_EDITORONLY_DATA
	: SoundSubmixGraph(nullptr)
#endif // WITH_EDITORONLY_DATA
{}

USoundSubmix::USoundSubmix(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bMuteWhenBackgrounded(0)
	, AmbisonicsPluginSettings(nullptr)
	, EnvelopeFollowerAttackTime(10)
	, EnvelopeFollowerReleaseTime(500)
	, GainMode(EGainParamMode::Linear)
	, OutputVolume(1.0f)
	, WetLevel(1.0f)
	, DryLevel(0.0f)
#if WITH_EDITOR
	, OutputVolumeDB(0.0f)
	, WetLevelDB(0.0f)
	, DryLevelDB(-120.0f)
#endif
{
}

void USoundSubmix::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	OutputVolumeDB = Audio::ConvertToDecibels(OutputVolume);
	WetLevelDB = Audio::ConvertToDecibels(WetLevel);
	DryLevelDB = Audio::ConvertToDecibels(DryLevel);
#endif

}

UEndpointSubmix::UEndpointSubmix(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, EndpointType(IAudioEndpointFactory::GetTypeNameForDefaultEndpoint())
{

}

USoundfieldSubmix::USoundfieldSubmix(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SoundfieldEncodingFormat(ISoundfieldFactory::GetFormatNameForInheritedEncoding())
{}

USoundfieldEndpointSubmix::USoundfieldEndpointSubmix(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SoundfieldEndpointType(ISoundfieldEndpointFactory::DefaultSoundfieldEndpointName())
{}

void USoundSubmix::StartRecordingOutput(const UObject* WorldContextObject, float ExpectedDuration)
{
	if (!GEngine)
	{
		return;
	}

	// Find device for this specific audio recording thing.
	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	FAudioDevice* DesiredAudioDevice = ThisWorld->GetAudioDeviceRaw();

	StartRecordingOutput(DesiredAudioDevice, ExpectedDuration);
}

void USoundSubmix::StartRecordingOutput(FAudioDevice* InDevice, float ExpectedDuration)
{
	if (InDevice)
	{
		InDevice->StartRecording(this, ExpectedDuration);
	}
}

void USoundSubmix::StopRecordingOutput(const UObject* WorldContextObject, EAudioRecordingExportType ExportType, const FString& Name, FString Path, USoundWave* ExistingSoundWaveToOverwrite)
{
	if (!GEngine)
	{
		return;
	}

	// Find device for this specific audio recording thing.
	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	FAudioDevice* DesiredAudioDevice = ThisWorld->GetAudioDeviceRaw();

	StopRecordingOutput(DesiredAudioDevice, ExportType, Name, Path, ExistingSoundWaveToOverwrite);
}

void USoundSubmix::StopRecordingOutput(FAudioDevice* InDevice, EAudioRecordingExportType ExportType, const FString& Name, FString Path, USoundWave* ExistingSoundWaveToOverwrite /*= nullptr*/)
{
	if (InDevice)
	{
		float SampleRate;
		float ChannelCount;

		Audio::AlignedFloatBuffer& RecordedBuffer = InDevice->StopRecording(this, ChannelCount, SampleRate);

		// This occurs when Stop Recording Output is called when Start Recording Output was not called.
		if (RecordedBuffer.Num() == 0)
		{
			return;
		}

		// Pack output data into DSPSampleBuffer and record it out!
		RecordingData.Reset(new Audio::FAudioRecordingData());

		RecordingData->InputBuffer = Audio::TSampleBuffer<int16>(RecordedBuffer, ChannelCount, SampleRate);

		switch (ExportType)
		{
			case EAudioRecordingExportType::SoundWave:
			{
				// If we're using the editor, we can write out a USoundWave to the content directory. Otherwise, we just generate a USoundWave without writing it to disk.
				if (GIsEditor)
				{
					RecordingData->Writer.BeginWriteToSoundWave(Name, RecordingData->InputBuffer, Path, [this](const USoundWave* Result)
					{
						if (OnSubmixRecordedFileDone.IsBound())
						{
							OnSubmixRecordedFileDone.Broadcast(Result);
						}
					});
				}
				else
				{
					RecordingData->Writer.BeginGeneratingSoundWaveFromBuffer(RecordingData->InputBuffer, nullptr, [this](const USoundWave* Result)
					{
						if (OnSubmixRecordedFileDone.IsBound())
						{
							OnSubmixRecordedFileDone.Broadcast(Result);
						}
					});
				}
			}
			break;
			
			case EAudioRecordingExportType::WavFile:
			{
				RecordingData->Writer.BeginWriteToWavFile(RecordingData->InputBuffer, Name, Path, [this]()
				{
					if (OnSubmixRecordedFileDone.IsBound())
					{
						OnSubmixRecordedFileDone.Broadcast(nullptr);
					}
				});
			}
			break;

			default:
			break;
		}
	}
}

void USoundSubmix::StartEnvelopeFollowing(const UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return;
	}

	// Find device for this specific audio recording thing.
	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	FAudioDevice* AudioDevice = ThisWorld->GetAudioDeviceRaw();

	StartEnvelopeFollowing(AudioDevice);
}

void USoundSubmix::StartEnvelopeFollowing(FAudioDevice* InAudioDevice)
{
	if (InAudioDevice)
	{
		InAudioDevice->StartEnvelopeFollowing(this);
	}
}

void USoundSubmix::StopEnvelopeFollowing(const UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return;
	}

	// Find device for this specific audio recording thing.
	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	FAudioDevice* AudioDevice = ThisWorld->GetAudioDeviceRaw();

	StopEnvelopeFollowing(AudioDevice);
}

void USoundSubmix::StopEnvelopeFollowing(FAudioDevice* InAudioDevice)
{
	if (InAudioDevice)
	{
		InAudioDevice->StopEnvelopeFollowing(this);
	}
}

void USoundSubmix::AddEnvelopeFollowerDelegate(const UObject* WorldContextObject, const FOnSubmixEnvelopeBP& OnSubmixEnvelopeBP)
{
	if (!GEngine)
	{
		return;
	}

	// Find device for this specific audio recording thing.
	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	FAudioDevice* AudioDevice = ThisWorld->GetAudioDeviceRaw();
	if (AudioDevice)
	{
		AudioDevice->AddEnvelopeFollowerDelegate(this, OnSubmixEnvelopeBP);
	}
}

void USoundSubmix::SetSubmixOutputVolume(const UObject* WorldContextObject, float InOutputVolume)
{
	if (!GEngine)
	{
		return;
	}

	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	FAudioDevice* AudioDevice = ThisWorld->GetAudioDeviceRaw();
	if (AudioDevice)
	{
		AudioDevice->SetSubmixOutputVolume(this, InOutputVolume);
	}
}

#if WITH_EDITOR
void USoundSubmix::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		static const FName NAME_OutputVolume(TEXT("OutputVolume"));
		static const FName NAME_WetLevel(TEXT("WetLevel"));
		static const FName NAME_DryLevel(TEXT("DryLevel"));
		static const FName NAME_OutputVolumeDB(TEXT("OutputVolumeDB"));
		static const FName NAME_WetLevelDB(TEXT("WetLevelDB"));
		static const FName NAME_DryLevelDB(TEXT("DryLevelDB"));

		FName ChangedPropName = PropertyChangedEvent.Property->GetFName();

		bool bUpdateSubmixGain = false;

		if (ChangedPropName == NAME_OutputVolume)
		{
			OutputVolumeDB = Audio::ConvertToDecibels(OutputVolume);
			bUpdateSubmixGain = true;
		}
		else if (ChangedPropName == NAME_WetLevel)
		{
			DryLevelDB = Audio::ConvertToDecibels(DryLevel);
			bUpdateSubmixGain = true;
		}
		else if (ChangedPropName == NAME_DryLevel)
		{
			WetLevelDB = Audio::ConvertToDecibels(OutputVolume);
			bUpdateSubmixGain = true;
		}
		else if (ChangedPropName == NAME_OutputVolumeDB)
		{
			if (OutputVolumeDB <= -120.f)
			{
				OutputVolume = 0.0f;
			}
			else
			{
				OutputVolume = Audio::ConvertToLinear(OutputVolumeDB);
			}
			bUpdateSubmixGain = true;
		}
		else if (ChangedPropName == NAME_WetLevelDB)
		{
			if (WetLevelDB <= -120.f)
			{
				WetLevel = 0.0f;
			}
			else
			{
				WetLevel = Audio::ConvertToLinear(WetLevelDB);
			}
			bUpdateSubmixGain = true;
		}
		else if (ChangedPropName == NAME_DryLevelDB)
		{
			if (DryLevelDB <= -120.0f)
			{
				DryLevel = 0.0f;
			}
			else
			{
				DryLevel = Audio::ConvertToLinear(DryLevelDB);
			}
			bUpdateSubmixGain = true;
		}

		// Force the properties to be initialized for this SoundSubmix on all active audio devices
		if (FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager())
		{
			if (bUpdateSubmixGain)
			{
//				AudioDeviceManager->UpdateSubmix(this);

				const float NewOutputVolume = OutputVolume;
				const float NewWetLevel = WetLevel;
				const float NewDryLevel = DryLevel;
				USoundSubmix* SoundSubmix = this;
				AudioDeviceManager->IterateOverAllDevices([SoundSubmix, NewOutputVolume, NewWetLevel, NewDryLevel](Audio::FDeviceId Id, FAudioDevice* Device)
				{
					Device->SetSubmixWetDryLevel(SoundSubmix, NewOutputVolume, NewWetLevel, NewDryLevel);
				});
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

FString USoundSubmixBase::GetDesc()
{
	return FString(TEXT("Sound Submix"));
}

void USoundSubmixBase::BeginDestroy()
{
	Super::BeginDestroy();

	// Use the main/default audio device for storing and retrieving sound class properties
	FAudioDeviceManager* AudioDeviceManager = (GEngine ? GEngine->GetAudioDeviceManager() : nullptr);

	// Force the properties to be initialized for this SoundClass on all active audio devices
	if (AudioDeviceManager)
	{
		AudioDeviceManager->UnregisterSoundSubmix(this);
	}
}

void USoundSubmixBase::PostLoad()
{
	Super::PostLoad();

	if (ClearBrokenSubmixAssetsCVar)
	{
		for (int32 ChildIndex = ChildSubmixes.Num() - 1; ChildIndex >= 0; ChildIndex--)
		{
			USoundSubmixBase* ChildSubmix = ChildSubmixes[ChildIndex];

			if (!ChildSubmix)
			{
				continue;
			}

			if (USoundSubmixWithParentBase* CastedChildSubmix = Cast<USoundSubmixWithParentBase>(ChildSubmix))
			{
				if (!ensure(CastedChildSubmix->ParentSubmix == this))
				{
					UE_LOG(LogAudio, Warning, TEXT("Submix had a child submix that didn't explicitly mark this submix as a parent!"));
					ChildSubmixes.RemoveAtSwap(ChildIndex);
				}
			}
			else
			{
				ensureMsgf(false, TEXT("Submix had a child submix that doesn't have an output!"));
				ChildSubmixes.RemoveAtSwap(ChildIndex);
			}
		}
	}

	// Use the main/default audio device for storing and retrieving sound class properties
	FAudioDeviceManager* AudioDeviceManager = (GEngine ? GEngine->GetAudioDeviceManager() : nullptr);

	// Force the properties to be initialized for this SoundClass on all active audio devices
	if (AudioDeviceManager)
	{
		AudioDeviceManager->RegisterSoundSubmix(this);
	}
}

#if WITH_EDITOR

void USoundSubmixBase::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	if (DuplicateMode == EDuplicateMode::Normal)
	{
		ChildSubmixes.Reset();
	}
}

void USoundSubmixBase::PreEditChange(FProperty* PropertyAboutToChange)
{
	static FName NAME_ChildSubmixes(TEXT("ChildSubmixes"));

	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == NAME_ChildSubmixes)
	{
		// Take a copy of the current state of child classes
		BackupChildSubmixes = ChildSubmixes;
	}
}

void USoundSubmixBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!GEngine)
	{
		return;
	}

	if (PropertyChangedEvent.Property != nullptr)
	{
		static const FName NAME_ChildSubmixes(TEXT("ChildSubmixes"));

		if (PropertyChangedEvent.Property->GetFName() == NAME_ChildSubmixes)
		{
			// Find child that was changed/added
			for (int32 ChildIndex = 0; ChildIndex < ChildSubmixes.Num(); ChildIndex++)
			{
				if (ChildSubmixes[ChildIndex] != nullptr && !BackupChildSubmixes.Contains(ChildSubmixes[ChildIndex]))
				{
					if (ChildSubmixes[ChildIndex]->RecurseCheckChild(this))
					{
						// Contains cycle so revert to old layout - launch notification to inform user
						FNotificationInfo Info(NSLOCTEXT("Engine", "UnableToChangeSoundSubmixChildDueToInfiniteLoopNotification", "Could not change SoundSubmix child as it would create a loop"));
						Info.ExpireDuration = 5.0f;
						Info.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Error"));
						FSlateNotificationManager::Get().AddNotification(Info);

						// Revert to the child submixes
						ChildSubmixes = BackupChildSubmixes;
					}
					else if (USoundSubmixWithParentBase* SubmixWithParent = CastChecked<USoundSubmixWithParentBase>(ChildSubmixes[ChildIndex]))
					{
						// Update parentage
						SubmixWithParent->SetParentSubmix(this);
					}
					break;
				}
			}

			// Update old child's parent if it has been removed
			for (int32 ChildIndex = 0; ChildIndex < BackupChildSubmixes.Num(); ChildIndex++)
			{
				if (BackupChildSubmixes[ChildIndex] != nullptr && !ChildSubmixes.Contains(BackupChildSubmixes[ChildIndex]))
				{
					BackupChildSubmixes[ChildIndex]->Modify();
					if (USoundSubmixWithParentBase* SubmixWithParent = Cast<USoundSubmixWithParentBase>(BackupChildSubmixes[ChildIndex]))
					{
						SubmixWithParent->ParentSubmix = nullptr;
					}
				}
			}

			// Force the properties to be initialized for this SoundSubmix on all active audio devices
			if (FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager())
			{
				AudioDeviceManager->RegisterSoundSubmix(this);
			}
		}
	}

	BackupChildSubmixes.Reset();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

TArray<USoundSubmixBase*> USoundSubmixBase::BackupChildSubmixes;

bool USoundSubmixBase::RecurseCheckChild(const USoundSubmixBase* ChildSoundSubmix) const
{
	for (int32 Index = 0; Index < ChildSubmixes.Num(); Index++)
	{
		if (ChildSubmixes[Index])
		{
			if (ChildSubmixes[Index] == ChildSoundSubmix)
			{
				return true;
			}

			if (ChildSubmixes[Index]->RecurseCheckChild(ChildSoundSubmix))
			{
				return true;
			}
		}
	}

	return false;
}

void USoundSubmixWithParentBase::SetParentSubmix(USoundSubmixBase* InParentSubmix)
{
	if (ParentSubmix != InParentSubmix)
	{
		if (ParentSubmix != nullptr)
		{
			ParentSubmix->Modify();
			ParentSubmix->ChildSubmixes.Remove(this);
		}

		Modify();
		ParentSubmix = InParentSubmix;
		ParentSubmix->ChildSubmixes.AddUnique(this);
		}
}

#if WITH_EDITOR
void USoundSubmixWithParentBase::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!GEngine)
	{
		return;
	}

	if (PropertyChangedEvent.Property != nullptr)
	{
		static const FName NAME_ParentSubmix(TEXT("ParentSubmix"));

		FName ChangedPropName = PropertyChangedEvent.Property->GetFName();

		if (ChangedPropName == NAME_ParentSubmix)
		{
			// Add this sound class to the parent class if it's not already added
			if (ParentSubmix)
			{
				bool bIsChildSubmix = false;
				for (int32 i = 0; i < ParentSubmix->ChildSubmixes.Num(); ++i)
				{
					USoundSubmixBase* ChildSubmix = ParentSubmix->ChildSubmixes[i];
					if (ChildSubmix && ChildSubmix == this)
					{
						bIsChildSubmix = true;
						break;
					}
				}

				if (!bIsChildSubmix)
				{
					ParentSubmix->Modify();
					ParentSubmix->ChildSubmixes.AddUnique(this);
				}
			}

			Modify();

			// Force the properties to be initialized for this SoundSubmix on all active audio devices
			if (FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager())
			{
				AudioDeviceManager->RegisterSoundSubmix(this);
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void USoundSubmixWithParentBase::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	if (DuplicateMode == EDuplicateMode::Normal)
	{
		SetParentSubmix(nullptr);
	}

	Super::PostDuplicate(DuplicateMode);
}
#endif

void USoundSubmixBase::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	USoundSubmixBase* This = CastChecked<USoundSubmixBase>(InThis);

	Collector.AddReferencedObject(This->SoundSubmixGraph, This);

	for (USoundSubmixBase* Backup : This->BackupChildSubmixes)
	{
		Collector.AddReferencedObject(Backup);
	}

	Super::AddReferencedObjects(InThis, Collector);
}
#endif // WITH_EDITOR

ISoundfieldFactory* USoundfieldSubmix::GetSoundfieldFactoryForSubmix() const
{
	// If this isn't called in the game thread, a ParentSubmix could get destroyed while we are recursing through the submix graph.
	ensure(IsInGameThread());

	FName SoundfieldFormat = GetSubmixFormat();
	check(SoundfieldFormat != ISoundfieldFactory::GetFormatNameForInheritedEncoding());

	return ISoundfieldFactory::Get(SoundfieldFormat);
}

const USoundfieldEncodingSettingsBase* USoundfieldSubmix::GetSoundfieldEncodingSettings() const
{
	return GetEncodingSettings();
}

TArray<USoundfieldEffectBase *> USoundfieldSubmix::GetSoundfieldProcessors() const
{
	return SoundfieldEffectChain;
}

FName USoundfieldSubmix::GetSubmixFormat() const
{
	USoundfieldSubmix* ParentSoundfieldSubmix = Cast<USoundfieldSubmix>(ParentSubmix);

	if (!ParentSoundfieldSubmix || SoundfieldEncodingFormat != ISoundfieldFactory::GetFormatNameForInheritedEncoding())
	{
		if (SoundfieldEncodingFormat == ISoundfieldFactory::GetFormatNameForInheritedEncoding())
		{
			return ISoundfieldFactory::GetFormatNameForNoEncoding();
		}
		else
			{
			return SoundfieldEncodingFormat;
			}

	}
	else if(ParentSoundfieldSubmix)
			{
		// If this submix matches the format of whatever submix it's plugged into, 
		// Recurse into the submix graph to find it.
		return ParentSoundfieldSubmix->GetSubmixFormat();
			}
	else
	{
		return ISoundfieldFactory::GetFormatNameForNoEncoding();
		}
}

const USoundfieldEncodingSettingsBase* USoundfieldSubmix::GetEncodingSettings() const
{
	FName SubmixFormatName = GetSubmixFormat();

	USoundfieldSubmix* ParentSoundfieldSubmix = Cast<USoundfieldSubmix>(ParentSubmix);

	if (EncodingSettings)
	{
		return EncodingSettings;
	}
	else if (ParentSoundfieldSubmix && SoundfieldEncodingFormat == ISoundfieldFactory::GetFormatNameForInheritedEncoding())
		{
		// If this submix matches the format of whatever it's plugged into,
		// Recurse into the submix graph to match it's settings.
		return ParentSoundfieldSubmix->GetEncodingSettings();
		}
	else if (ISoundfieldFactory* Factory = ISoundfieldFactory::Get(SubmixFormatName))
		{
		// If we don't have any encoding settings, use the default.
		return Factory->GetDefaultEncodingSettings();
		}
	else
	{
		// If we don't have anything, exit.
		return nullptr;
	}
}

IAudioEndpointFactory* UEndpointSubmix::GetAudioEndpointForSubmix() const
{
	return IAudioEndpointFactory::Get(EndpointType);
}

const UAudioEndpointSettingsBase* UEndpointSubmix::GetEndpointSettings() const
{
	return EndpointSettings;
}

ISoundfieldEndpointFactory* USoundfieldEndpointSubmix::GetSoundfieldEndpointForSubmix() const
{
	return ISoundfieldEndpointFactory::Get(SoundfieldEndpointType);
}

const USoundfieldEndpointSettingsBase* USoundfieldEndpointSubmix::GetEndpointSettings() const
{
	return EndpointSettings;
}

const USoundfieldEncodingSettingsBase* USoundfieldEndpointSubmix::GetEncodingSettings() const
{
	return EncodingSettings;
}

TArray<USoundfieldEffectBase*> USoundfieldEndpointSubmix::GetSoundfieldProcessors() const
{
	return SoundfieldEffectChain;
}

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AudioSettings.cpp: Unreal audio settings
=============================================================================*/

#include "Sound/AudioSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundNodeQualityLevel.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "AudioSettings"

UAudioSettings::UAudioSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SectionName = TEXT("Audio");
	AddDefaultSettings();

	bAllowPlayWhenSilent = true;
	bIsAudioMixerEnabled = false;

	GlobalMinPitchScale = 0.4F;
	GlobalMaxPitchScale = 2.0F;
}

void UAudioSettings::AddDefaultSettings()
{
	FAudioQualitySettings DefaultSettings;
	DefaultSettings.DisplayName = LOCTEXT("DefaultSettingsName", "Default");
	QualityLevels.Add(DefaultSettings);
	bAllowPlayWhenSilent = true;
	DefaultReverbSendLevel_DEPRECATED = 0.0f;
	VoiPSampleRate = EVoiceSampleRate::Low16000Hz;
	NumStoppingSources = 8;
}

#if WITH_EDITOR
void UAudioSettings::PreEditChange(FProperty* PropertyAboutToChange)
{
	// Cache ambisonic submix in case user tries to set to submix that isn't set to ambisonics
	CachedAmbisonicSubmix = AmbisonicSubmix;

	// Cache at least the first entry in case someone tries to clear the array
	CachedQualityLevels = QualityLevels;
}

void UAudioSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property)
	{
		bool bReconcileQualityNodes = false;
		bool bPromptRestartRequired = false;
		FName PropertyName = PropertyChangedEvent.Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UAudioSettings, MasterSubmix)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UAudioSettings, EQSubmix)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UAudioSettings, ReverbSubmix))
		{
			bPromptRestartRequired = true;
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UAudioSettings, AmbisonicSubmix))
		{
			if (USoundSubmix* NewSubmix = Cast<USoundSubmix>(AmbisonicSubmix.TryLoad()))
			{
				if (NewSubmix->ChannelFormat != ESubmixChannelFormat::Ambisonics)
				{
					FNotificationInfo Info(LOCTEXT("AudioSettings_InvalidAmbisonicSubmixFormat",
						"Ambisonic Submix format must be set to 'Ambisonics' in order to be set as 'Master Ambisonics Submix'."));
					Info.bFireAndForget = true;
					Info.ExpireDuration = 2.0f;
					Info.bUseThrobber = true;
					FSlateNotificationManager::Get().AddNotification(Info);

					AmbisonicSubmix = CachedAmbisonicSubmix;
				}
			}
			else
			{
				bPromptRestartRequired = true;
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UAudioSettings, QualityLevels))
		{
			if (QualityLevels.Num() == 0)
			{
				QualityLevels.Add(CachedQualityLevels[0]);
			}
			else if (QualityLevels.Num() > CachedQualityLevels.Num())
			{
				for (FAudioQualitySettings& AQSettings : QualityLevels)
				{
					if (AQSettings.DisplayName.IsEmpty())
					{
						bool bFoundDuplicate;
						int32 NewQualityLevelIndex = 0;
						FText NewLevelName;
						do
						{
							bFoundDuplicate = false;
							NewLevelName = FText::Format(LOCTEXT("NewQualityLevelName","New Level{0}"), (NewQualityLevelIndex > 0 ? FText::FromString(FString::Printf(TEXT(" %d"),NewQualityLevelIndex)) : FText::GetEmpty()));
							for (const FAudioQualitySettings& QualityLevelSettings : QualityLevels)
							{
								if (QualityLevelSettings.DisplayName.EqualTo(NewLevelName))
								{
									bFoundDuplicate = true;
									break;
								}
							}
							NewQualityLevelIndex++;
						} while (bFoundDuplicate);
						AQSettings.DisplayName = NewLevelName;
					}
				}
			}

			bReconcileQualityNodes = true;
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FAudioQualitySettings, DisplayName))
		{
			bReconcileQualityNodes = true;
		}

		if (bReconcileQualityNodes)
		{
			for (TObjectIterator<USoundNodeQualityLevel> It; It; ++It)
			{
				It->ReconcileNode(true);
			}
		}

		if (bPromptRestartRequired)
		{
			FNotificationInfo Info(LOCTEXT("AudioSettings_ChangeRequiresEditorRestart",
				"Change to Audio Settings requires editor restart in order for changes to take effect."));
			Info.bFireAndForget = true;
			Info.ExpireDuration = 2.0f;
			Info.bUseThrobber = true;
			FSlateNotificationManager::Get().AddNotification(Info);
		}

		AudioSettingsChanged.Broadcast();
	}
}
#endif // WITH_EDITOR

const FAudioQualitySettings& UAudioSettings::GetQualityLevelSettings(int32 QualityLevel) const
{
	check(QualityLevels.Num() > 0);
	return QualityLevels[FMath::Clamp(QualityLevel, 0, QualityLevels.Num() - 1)];
}

int32 UAudioSettings::GetQualityLevelSettingsNum() const
{
	return QualityLevels.Num();
}


void UAudioSettings::SetAudioMixerEnabled(const bool bInAudioMixerEnabled)
{
	bIsAudioMixerEnabled = bInAudioMixerEnabled;
}

const bool UAudioSettings::IsAudioMixerEnabled() const
{
	return bIsAudioMixerEnabled;
}

int32 UAudioSettings::GetHighestMaxChannels() const
{
	check(QualityLevels.Num() > 0);

	int32 HighestMaxChannels = -1;
	for (const FAudioQualitySettings& Settings : QualityLevels)
	{
		if (Settings.MaxChannels > HighestMaxChannels)
		{
			HighestMaxChannels = Settings.MaxChannels;
		}
	}

	return HighestMaxChannels;
}

FString UAudioSettings::FindQualityNameByIndex(int32 Index) const
{
	return QualityLevels.IsValidIndex(Index) ?
		   QualityLevels[Index].DisplayName.ToString() :
		   TEXT("");
}

#undef LOCTEXT_NAMESPACE

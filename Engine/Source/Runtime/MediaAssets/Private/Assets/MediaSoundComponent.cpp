// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MediaSoundComponent.h"
#include "MediaAssetsPrivate.h"

#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "IMediaAudioSample.h"
#include "IMediaPlayer.h"
#include "MediaAudioResampler.h"
#include "Misc/ScopeLock.h"
#include "Sound/AudioSettings.h"
#include "UObject/UObjectGlobals.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include "MediaPlayer.h"
#include "MediaPlayerFacade.h"


DECLARE_FLOAT_COUNTER_STAT(TEXT("MediaUtils MediaSoundComponent Sync"), STAT_MediaUtils_MediaSoundComponentSync, STATGROUP_Media);
DECLARE_FLOAT_COUNTER_STAT(TEXT("MediaUtils MediaSoundComponent SampleTime"), STAT_MediaUtils_MediaSoundComponentSampleTime, STATGROUP_Media);
DECLARE_DWORD_COUNTER_STAT(TEXT("MediaUtils MediaSoundComponent Queued"), STAT_Media_SoundCompQueued, STATGROUP_Media);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(MEDIA_API, MediaStreaming);

/* Static initialization
 *****************************************************************************/

USoundClass* UMediaSoundComponent::DefaultMediaSoundClassObject = nullptr;


/* UMediaSoundComponent structors
 *****************************************************************************/

UMediaSoundComponent::UMediaSoundComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Channels(EMediaSoundChannels::Stereo)
	, DynamicRateAdjustment(false)
	, RateAdjustmentFactor(0.00000001f)
	, RateAdjustmentRange(FFloatRange(0.995f, 1.005f))
	, CachedRate(0.0f)
	, CachedTime(FTimespan::Zero())
	, RateAdjustment(1.0f)
	, Resampler(new FMediaAudioResampler)
	, LastPlaySampleTime(FTimespan::MinValue())
	, EnvelopeFollowerAttackTime(10)
	, EnvelopeFollowerReleaseTime(100)
	, CurrentEnvelopeValue(0.0f)
	, bSpectralAnalysisEnabled(false)
	, bEnvelopeFollowingEnabled(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	bAutoActivate = true;

#if PLATFORM_MAC
	PreferredBufferLength = 4 * 1024; // increase buffer callback size on macOS to prevent underruns
#endif

#if WITH_EDITORONLY_DATA
	bVisualizeComponent = true;
#endif
}


UMediaSoundComponent::~UMediaSoundComponent()
{
	delete Resampler;
}


/* UMediaSoundComponent interface
 *****************************************************************************/

bool UMediaSoundComponent::BP_GetAttenuationSettingsToApply(FSoundAttenuationSettings& OutAttenuationSettings)
{
	const FSoundAttenuationSettings* SelectedAttenuationSettings = GetSelectedAttenuationSettings();

	if (SelectedAttenuationSettings == nullptr)
	{
		return false;
	}

	OutAttenuationSettings = *SelectedAttenuationSettings;

	return true;
}


UMediaPlayer* UMediaSoundComponent::GetMediaPlayer() const
{
	return CurrentPlayer.Get();
}


void UMediaSoundComponent::SetMediaPlayer(UMediaPlayer* NewMediaPlayer)
{
	CurrentPlayer = NewMediaPlayer;
}

#if WITH_EDITOR

void UMediaSoundComponent::SetDefaultMediaPlayer(UMediaPlayer* NewMediaPlayer)
{
	MediaPlayer = NewMediaPlayer;
	CurrentPlayer = MediaPlayer;
}

#endif


void UMediaSoundComponent::UpdatePlayer()
{
	UMediaPlayer* CurrentPlayerPtr = CurrentPlayer.Get();
	if (CurrentPlayerPtr == nullptr)
	{
		CachedRate = 0.0f;
		CachedTime = FTimespan::Zero();

		FScopeLock Lock(&CriticalSection);
		SampleQueue.Reset();

		return;
	}

	// create a new sample queue if the player changed
	TSharedRef<FMediaPlayerFacade, ESPMode::ThreadSafe> PlayerFacade = CurrentPlayerPtr->GetPlayerFacade();

	// We have some audio decoders which are running with a limited amount of pre-allocated audio sample packets. 
	// When the audio packets are not consumed in the UMediaSoundComponent::OnGenerateAudio method below, these packets are not 
	// returned to the decooder which then cannot produce more audio samples. 
	//
	// The UMediaSoundComponent::OnGenerateAudio is only called when our parent USynthComponent it active and
	// this is conrolled by USynthComponent::Start() and USynthComponent::Stop(). We are tracking a state change here.
	if (PlayerFacade != CurrentPlayerFacade)
	{
		if (IsActive())
		{
			const auto NewSampleQueue = MakeShared<FMediaAudioSampleQueue, ESPMode::ThreadSafe>();
			PlayerFacade->AddAudioSampleSink(NewSampleQueue);
			{
				FScopeLock Lock(&CriticalSection);
				SampleQueue = NewSampleQueue;
			}
			CurrentPlayerFacade = PlayerFacade;
		}
	}
	else
	{
		// Here, we have a CurrentPlayerFacade set which means are also have a valid FMediaAudioSampleQueue set
		// We need to check for deactivation as it seems there is not callback scheduled when USynthComponent::Stop() is called.
		if(!IsActive())
		{
			FScopeLock Lock(&CriticalSection);
			SampleQueue.Reset();
			CurrentPlayerFacade.Reset();
		}
	}

	// caching play rate and time for audio thread (eventual consistency is sufficient)
	CachedRate = PlayerFacade->GetRate();
	CachedTime = PlayerFacade->GetTime();

	PlayerFacade->SetLastAudioRenderedSampleTime(LastPlaySampleTime.Load());
}


/* TAttenuatedComponentVisualizer interface
 *****************************************************************************/

void UMediaSoundComponent::CollectAttenuationShapesForVisualization(TMultiMap<EAttenuationShape::Type, FBaseAttenuationSettings::AttenuationShapeDetails>& ShapeDetailsMap) const
{
	const FSoundAttenuationSettings* SelectedAttenuationSettings = GetSelectedAttenuationSettings();

	if (SelectedAttenuationSettings != nullptr)
	{
		SelectedAttenuationSettings->CollectAttenuationShapesForVisualization(ShapeDetailsMap);
	}
}


/* UActorComponent interface
 *****************************************************************************/

void UMediaSoundComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	if (SpriteComponent != nullptr)
	{
		SpriteComponent->SpriteInfo.Category = TEXT("Sounds");
		SpriteComponent->SpriteInfo.DisplayName = NSLOCTEXT("SpriteCategory", "Sounds", "Sounds");

		if (bAutoActivate)
		{
			SpriteComponent->SetSprite(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/AudioIcons/S_AudioComponent_AutoActivate.S_AudioComponent_AutoActivate")));
		}
		else
		{
			SpriteComponent->SetSprite(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/AudioIcons/S_AudioComponent.S_AudioComponent")));
		}
	}
#endif
}

void UMediaSoundComponent::OnUnregister()
{
	{
		FScopeLock Lock(&CriticalSection);
		SampleQueue.Reset();
	}
	CurrentPlayerFacade.Reset();
	Super::OnUnregister();
}


void UMediaSoundComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdatePlayer();
}


/* USceneComponent interface
 *****************************************************************************/

void UMediaSoundComponent::Activate(bool bReset)
{
	if (bReset || ShouldActivate())
	{
		SetComponentTickEnabled(true);
	}

	Super::Activate(bReset);
}


void UMediaSoundComponent::Deactivate()
{
	if (!ShouldActivate())
	{
		SetComponentTickEnabled(false);
		{
			FScopeLock Lock(&CriticalSection);
			SampleQueue.Reset();
		}
		CurrentPlayerFacade.Reset();
	}
	Super::Deactivate();
}


/* UObject interface
 *****************************************************************************/

void UMediaSoundComponent::PostInitProperties()
{
	Super::PostInitProperties();

	if (UMediaSoundComponent::DefaultMediaSoundClassObject == nullptr)
	{
		const FSoftObjectPath DefaultMediaSoundClassName = GetDefault<UAudioSettings>()->DefaultMediaSoundClassName;

		if (DefaultMediaSoundClassName.IsValid())
		{
			UMediaSoundComponent::DefaultMediaSoundClassObject = LoadObject<USoundClass>(nullptr, *DefaultMediaSoundClassName.ToString());
		}
	}

	// We have a different default sound class object for media sound components
	if (SoundClass == USoundBase::DefaultSoundClassObject || SoundClass == nullptr)
	{
		SoundClass = UMediaSoundComponent::DefaultMediaSoundClassObject;
	}
}


void UMediaSoundComponent::PostLoad()
{
	Super::PostLoad();

	CurrentPlayer = MediaPlayer;
}


#if WITH_EDITOR

void UMediaSoundComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName MediaPlayerName = GET_MEMBER_NAME_CHECKED(UMediaSoundComponent, MediaPlayer);

	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (PropertyThatChanged != nullptr)
	{
		const FName PropertyName = PropertyThatChanged->GetFName();

		if (PropertyName == MediaPlayerName)
		{
			CurrentPlayer = MediaPlayer;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif //WITH_EDITOR


/* USynthComponent interface
 *****************************************************************************/

bool UMediaSoundComponent::Init(int32& SampleRate)
{
	Super::Init(SampleRate);

	// Initialize the settings for the spectrum analyzer
	SpectrumAnalyzer.Init(SampleRate);

	if (Channels == EMediaSoundChannels::Mono)
	{
		NumChannels = 1;
	}
	else //if (Channels == EMediaSoundChannels::Stereo)
	{
		NumChannels = 2;
	}/*
	else
	{
		NumChannels = 8;
	}*/

	// increase buffer callback size for media decoding. Media doesn't need fast response time so can decode more per callback.
	//PreferredBufferLength = NumChannels * 8196;

	Resampler->Initialize(NumChannels, SampleRate);

	return true;
}


int32 UMediaSoundComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	CSV_SCOPED_TIMING_STAT(MediaStreaming, UMediaSoundComponent_OnGenerateAudio);

	int32 InitialSyncOffset = 0;
	TSharedPtr<FMediaAudioSampleQueue, ESPMode::ThreadSafe> PinnedSampleQueue;
	{
		FScopeLock Lock(&CriticalSection);
		PinnedSampleQueue = SampleQueue;
	}

	if (PinnedSampleQueue.IsValid() && (CachedRate != 0.0f))
	{
		const float Rate = CachedRate.Load();
		const FTimespan Time = CachedTime.Load();

		FTimespan OutTime = FTimespan::Zero();

		{
			const uint32 FramesRequested = uint32(NumSamples / NumChannels);
			uint32 JumpFrame = MAX_uint32;
			uint32 FramesWritten = Resampler->Generate(OutAudio, OutTime, FramesRequested, Rate, Time, *PinnedSampleQueue, JumpFrame);
			if (FramesWritten == 0)
			{
				return 0; // no samples available
			}

			if (FramesWritten < FramesRequested)
			{
				memset(OutAudio + FramesWritten * NumChannels, 0, (NumSamples - FramesWritten * NumChannels) * sizeof(float));
			}
		}

		LastPlaySampleTime = OutTime;


		if (bSpectralAnalysisEnabled || bEnvelopeFollowingEnabled)
		{
			float* BufferToUseForAnalysis = nullptr;
			int32 NumFrames = NumSamples;
			
			if (NumChannels == 2)
			{
				NumFrames = NumSamples / 2;

				// Use the scratch buffer to sum the audio to mono
				AudioScratchBuffer.Reset();
				AudioScratchBuffer.AddUninitialized(NumFrames);
				BufferToUseForAnalysis = AudioScratchBuffer.GetData();
				int32 SampleIndex = 0;
				for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex, SampleIndex += NumChannels)
				{
					BufferToUseForAnalysis[FrameIndex] = 0.5f * (OutAudio[SampleIndex] + OutAudio[SampleIndex + 1]);
				}
			}
			else
			{
				BufferToUseForAnalysis = OutAudio;
			}

			if (bSpectralAnalysisEnabled)
			{
				SpectrumAnalyzer.PushAudio(BufferToUseForAnalysis, NumFrames);
				SpectrumAnalyzer.PerformAnalysisIfPossible(true, true);
			}

			{
				FScopeLock ScopeLock(&EnvelopeFollowerCriticalSection);
				if (bEnvelopeFollowingEnabled)
				{
					if (bEnvelopeFollowerSettingsChanged)
					{
						EnvelopeFollower.SetAttackTime((float)EnvelopeFollowerAttackTime);
						EnvelopeFollower.SetReleaseTime((float)EnvelopeFollowerReleaseTime);

						bEnvelopeFollowerSettingsChanged = false;
					}

					for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
					{
						EnvelopeFollower.ProcessAudio(BufferToUseForAnalysis[FrameIndex]);
					}

					CurrentEnvelopeValue = EnvelopeFollower.GetCurrentValue();
				}
			}
		}

		SET_FLOAT_STAT(STAT_MediaUtils_MediaSoundComponentSync, FMath::Abs((Time - OutTime).GetTotalMilliseconds()));
		SET_FLOAT_STAT(STAT_MediaUtils_MediaSoundComponentSampleTime, OutTime.GetTotalMilliseconds());
		SET_DWORD_STAT(STAT_Media_SoundCompQueued, PinnedSampleQueue->Num());
	}
	else
	{
		Resampler->Flush();

		LastPlaySampleTime = FTimespan::MinValue();
	}
	return NumSamples;
}

void UMediaSoundComponent::SetEnableSpectralAnalysis(bool bInSpectralAnalysisEnabled)
{
	bSpectralAnalysisEnabled = bInSpectralAnalysisEnabled;
}

void UMediaSoundComponent::SetSpectralAnalysisSettings(TArray<float> InFrequenciesToAnalyze, EMediaSoundComponentFFTSize InFFTSize)
{
	Audio::FSpectrumAnalyzerSettings::EFFTSize SpectrumAnalyzerSize;

	switch (InFFTSize)
	{
		case EMediaSoundComponentFFTSize::Min_64: 
			SpectrumAnalyzerSize = Audio::FSpectrumAnalyzerSettings::EFFTSize::Min_64;
			break;
		
		case EMediaSoundComponentFFTSize::Small_256: 
			SpectrumAnalyzerSize = Audio::FSpectrumAnalyzerSettings::EFFTSize::Small_256;
			break;
		
		default:
		case EMediaSoundComponentFFTSize::Medium_512:
			SpectrumAnalyzerSize = Audio::FSpectrumAnalyzerSettings::EFFTSize::Medium_512;
			break;

		case EMediaSoundComponentFFTSize::Large_1024: 
			SpectrumAnalyzerSize = Audio::FSpectrumAnalyzerSettings::EFFTSize::Large_1024;
			break;
	}

	SpectrumAnalyzerSettings.FFTSize = SpectrumAnalyzerSize;
	SpectrumAnalyzer.SetSettings(SpectrumAnalyzerSettings);

	FrequenciesToAnalyze = InFrequenciesToAnalyze;
}

TArray<FMediaSoundComponentSpectralData> UMediaSoundComponent::GetSpectralData()
{
	if (bSpectralAnalysisEnabled)
	{
		TArray<FMediaSoundComponentSpectralData> SpectralData;
		SpectrumAnalyzer.LockOutputBuffer();

		for (float Frequency : FrequenciesToAnalyze)
		{
			FMediaSoundComponentSpectralData Data;
			Data.FrequencyHz = Frequency;
			Data.Magnitude = SpectrumAnalyzer.GetMagnitudeForFrequency(Frequency);
			SpectralData.Add(Data);
		}
		SpectrumAnalyzer.UnlockOutputBuffer();

		return SpectralData;
	}
	// Empty array if spectrum analysis is not implemented
	return TArray<FMediaSoundComponentSpectralData>();
}

void UMediaSoundComponent::SetEnableEnvelopeFollowing(bool bInEnvelopeFollowing)
{
	FScopeLock ScopeLock(&EnvelopeFollowerCriticalSection);
	bEnvelopeFollowingEnabled = bInEnvelopeFollowing;
	CurrentEnvelopeValue = 0.0f;
}

void UMediaSoundComponent::SetEnvelopeFollowingsettings(int32 AttackTimeMsec, int32 ReleaseTimeMsec)
{
	FScopeLock ScopeLock(&EnvelopeFollowerCriticalSection);
	EnvelopeFollowerAttackTime = AttackTimeMsec;
	EnvelopeFollowerReleaseTime = ReleaseTimeMsec;
	bEnvelopeFollowerSettingsChanged = true;
}

float UMediaSoundComponent::GetEnvelopeValue() const
{
	return CurrentEnvelopeValue;
}

/* UMediaSoundComponent implementation
 *****************************************************************************/

const FSoundAttenuationSettings* UMediaSoundComponent::GetSelectedAttenuationSettings() const
{
	if (bOverrideAttenuation)
	{
		return &AttenuationOverrides;
	}
	
	if (AttenuationSettings != nullptr)
	{
		return &AttenuationSettings->Attenuation;
	}

	return nullptr;
}

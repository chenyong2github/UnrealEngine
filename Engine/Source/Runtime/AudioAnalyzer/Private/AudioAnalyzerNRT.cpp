// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioAnalyzerNRT.h"
#include "AudioAnalyzerNRTFacade.h"
#include "AudioAnalyzerModule.h"
#include "Sound/SampleBuffer.h"
#include "Async/Async.h"

#if WITH_EDITOR

namespace 
{
	class FAudioAnalyzeTask : public FNonAbandonableTask
	{
		friend class FAutoDeleteAsyncTask<FAudioAnalyzeTask>;

		public:
			FAudioAnalyzeTask(
					TWeakObjectPtr<UAudioAnalyzerNRT> InAnalyzerUObject, 
					TUniquePtr<Audio::FAnalyzerNRTBatch>&& InAnalyzerFacade, 
					TArray<uint8>&& InRawWaveData,
					int32 InNumChannels,
					float InSampleRate)
			: AnalyzerUObject(InAnalyzerUObject)
			, AnalyzerFacade(MoveTemp(InAnalyzerFacade))
			, RawWaveData(MoveTemp(InRawWaveData))
			, NumChannels(InNumChannels)
			, SampleRate(InSampleRate)
			{}

			void DoWork()
			{
				TUniquePtr<Audio::IAnalyzerNRTResult> Result = AnalyzerFacade->AnalyzePCM16Audio(RawWaveData, NumChannels, SampleRate);

				// Make sharedptr so can be passed by value to async lambda
				TSharedPtr<Audio::IAnalyzerNRTResult, ESPMode::ThreadSafe> ResultPtr(Result.Release());
				// Make local so can be passed to lambda without having to capture 'this'
				TWeakObjectPtr<UAudioAnalyzerNRT> Analyzer = AnalyzerUObject;

				// Set value on game thread.
				AsyncTask(ENamedThreads::GameThread, [Analyzer, ResultPtr]() {
					if (Analyzer.IsValid())
					{
						Analyzer->SetResult(ResultPtr);
					}
				});
			}

			FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(AudioAnalyzeTask, STATGROUP_ThreadPoolAsyncTasks); }

		private:
			TWeakObjectPtr<UAudioAnalyzerNRT> AnalyzerUObject;
			TUniquePtr<Audio::FAnalyzerNRTBatch> AnalyzerFacade;
			TArray<uint8> RawWaveData;
			int32 NumChannels;
			float SampleRate;
	};
}

/*****************************************************/
/*********** UAudioAnalyzerNRTSettings ***************/
/*****************************************************/

void UAudioAnalyzerNRTSettings::PostEditChangeProperty (struct FPropertyChangedEvent & PropertyChangedEvent) 
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (ShouldEventTriggerAnalysis(PropertyChangedEvent))
	{
		AnalyzeAudioDelegate.ExecuteIfBound();
	}
}

bool UAudioAnalyzerNRTSettings::ShouldEventTriggerAnalysis(struct FPropertyChangedEvent & PropertyChangeEvent)
{
	// By default, all changes to settings will trigger analysis.
	return true;
}


/*****************************************************/
/***********      UAudioAnalyzerNRT    ***************/
/*****************************************************/

void UAudioAnalyzerNRT::PreEditChange(UProperty* PropertyAboutToChange)
{
	// If the settings object is replaced, need to unbind any existing settings objects
	// from calling the analyze audio delegate.
	Super::PreEditChange(PropertyAboutToChange);
	
	UAudioAnalyzerNRTSettings* Settings = GetSettingsFromProperty(PropertyAboutToChange);

	if (Settings)
	{
		Settings->AnalyzeAudioDelegate.Unbind();
	}
}

void UAudioAnalyzerNRT::PostEditChangeProperty (struct FPropertyChangedEvent & PropertyChangedEvent) 
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	// Check if the edited property was a UAudioAnalyzerNRTSettings object
	UAudioAnalyzerNRTSettings* Settings = GetSettingsFromProperty(PropertyChangedEvent.Property);

	if (Settings)
	{
		// If it was a UAudioAnalyzerNRTSettings object, bind the FAnalyzeAudioDelegate
		Settings->AnalyzeAudioDelegate.BindUObject(this, &UAudioAnalyzerNRT::AnalyzeAudio);
	}

	if (ShouldEventTriggerAnalysis(PropertyChangedEvent))
	{
		AnalyzeAudio();
	}
}

bool UAudioAnalyzerNRT::ShouldEventTriggerAnalysis(struct FPropertyChangedEvent & PropertyChangeEvent)
{
	// by default, all changes will trigger analysis
	return true;
}

void UAudioAnalyzerNRT::AnalyzeAudio()
{
	TSharedPtr<Audio::IAnalyzerNRTResult, ESPMode::ThreadSafe> NewResult;

	if (nullptr != Sound)
	{
		// Read audio while Sound object is assured safe. 
		if (Sound->ChannelSizes.Num() > 0)
		{
			UE_LOG(LogAudioAnalyzer, Warning, TEXT("Soundwave '%s' has multi-channel audio (channels greater than 2). Audio analysis is not currently supported for this yet."), *Sound->GetFullName());
			return;
		}

		// Retrieve the raw imported data
		TArray<uint8> RawWaveData;
		uint32 SampleRate = 0;
		uint16 NumChannels = 0;

		if (!Sound->GetImportedSoundWaveData(RawWaveData, SampleRate, NumChannels))
		{
			UE_LOG(LogAudioAnalyzer, Error, TEXT("Could not analyze audio due to failed import of sound wave data from Soundwave '%s'."), *Sound->GetFullName());
			return;
		}

		if (SampleRate == 0 || NumChannels == 0)
		{
			UE_LOG(LogAudioAnalyzer, Error, TEXT("Failed to parse the raw imported data for '%s' for analysis."), *Sound->GetFullName());
			return;
		}
		
		// Create analyzer helper object
		TUniquePtr<Audio::FAnalyzerNRTBatch> BatchAnalyzer = MakeUnique<Audio::FAnalyzerNRTBatch>(GetSettings(), GetAnalyzerNRTFactoryName());

		// Use weak reference in case this object is deleted before analysis is done
		TWeakObjectPtr<UAudioAnalyzerNRT> AnalyzerPtr(this);
		
		// Create and start async task. Parentheses avoids memory leak warnings from static analysis.
		(new FAutoDeleteAsyncTask<FAudioAnalyzeTask>(AnalyzerPtr, MoveTemp(BatchAnalyzer), MoveTemp(RawWaveData), NumChannels, SampleRate))->StartBackgroundTask();
	}
	else
	{
		// Copy empty result to this object
		SetResult(nullptr);
	}
}

// Returns UAudioAnalyzerNRTSettings* if property points to a valid UAudioAnalyzerNRTSettings, otherwise returns nullptr.
UAudioAnalyzerNRTSettings* UAudioAnalyzerNRT::GetSettingsFromProperty(UProperty* Property)
{
	if (nullptr == Property)
	{
		return nullptr;
	}

	if (Property->IsA(UObjectPropertyBase::StaticClass()))
	{
		UObjectPropertyBase* ObjectPropertyBase = CastChecked<UObjectPropertyBase>(Property);
		
		if (nullptr == ObjectPropertyBase)
		{
			return nullptr;
		}

		if (ObjectPropertyBase->PropertyClass->IsChildOf(UAudioAnalyzerNRTSettings::StaticClass()))
		{
			UObject* PropertyObject = ObjectPropertyBase->GetObjectPropertyValue_InContainer(this);
			return Cast<UAudioAnalyzerNRTSettings>(PropertyObject);
		}
	}

	return nullptr;
}

void UAudioAnalyzerNRT::SetResult(TSharedPtr<Audio::IAnalyzerNRTResult, ESPMode::ThreadSafe> NewResult)
{
	FScopeLock ResultLock(&ResultCriticalSection);
	Result = NewResult;
}

#endif


void UAudioAnalyzerNRT::Serialize(FArchive& Ar)
{
	// default uobject serialize
	Super::Serialize(Ar);

	// When loading object, Result pointer is invalid. Need to create a valid 
	// result object for loading.
	if (!Result.IsValid())
	{
		if (!GetClass()->HasAnyClassFlags(CLASS_Abstract))
		{
			Audio::IAnalyzerNRTFactory* Factory = Audio::GetAnalyzerNRTFactory(GetAnalyzerNRTFactoryName());

			if (nullptr != Factory)
			{
				// Create result and worker from factory
				TUniquePtr<Audio::IAnalyzerNRTResult> NewResult = Factory->NewResult();
				{
					FScopeLock ResultLock(&ResultCriticalSection);
					Result = TSharedPtr<Audio::IAnalyzerNRTResult, ESPMode::ThreadSafe>(NewResult.Release());
				}
			}
		}
	}

	if (Result.IsValid())
	{
		FScopeLock ResultLock(&ResultCriticalSection);
		Result->Serialize(Ar);
	}
}

TUniquePtr<Audio::IAnalyzerNRTSettings> UAudioAnalyzerNRT::GetSettings()
{
	return MakeUnique<Audio::IAnalyzerNRTSettings>();
}


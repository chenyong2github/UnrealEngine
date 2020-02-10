// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaIOCoreSampleContainer.h"


template<typename SampleType>
void FMediaIOCoreSampleContainer<SampleType>::FlushSamples()
{
	FScopeLock Lock(&CriticalSection);
	Samples.Empty();
}

template<typename SampleType>
bool FMediaIOCoreSampleContainer<SampleType>::FetchSample(TRange<FTimespan> TimeRange, TSharedPtr<SampleType, ESPMode::ThreadSafe>& OutSample)
{
	FScopeLock Lock(&CriticalSection);

	const int32 SampleCount = Samples.Num();
	if (SampleCount > 0)
	{
		TSharedPtr<SampleType, ESPMode::ThreadSafe> Sample = Samples[SampleCount - 1];
		if (Sample.IsValid())
		{
			const FTimespan SampleTime = Sample->GetTime();

			if (TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
			{
				Samples.RemoveAt(SampleCount - 1);
				OutSample = Sample;
				return true;
			}
		}
	}

	return false;
}

template<typename SampleType>
void FMediaIOCoreSampleContainer<SampleType>::UpdateSettings(const FMediaIOSamplingSettings& InSettings)
{
	EvaluationSettings = InSettings;
	ResetBufferStats();
}

template<typename SampleType>
void FMediaIOCoreSampleContainer<SampleType>::CacheState(FTimespan PlayerTime)
{
	FScopeLock Lock(&CriticalSection);

	//Cache data for that tick. The player has decided the current time so we know the evaluation point. 
	const int32 SampleCount = Samples.Num();
	CachedSamplesData.Reset(SampleCount);
	if (SampleCount)
	{
		for (const TSharedPtr<SampleType, ESPMode::ThreadSafe>& Sample : Samples)
		{
			if (Sample.IsValid())
			{
				FTimedDataChannelSampleTime& NewestSampleTime = CachedSamplesData.Emplace_GetRef();
				NewestSampleTime.PlatformSecond = Sample->GetTime().GetTotalSeconds() - EvaluationSettings.PlayerTimeOffset;
				if (Sample->GetTimecode().IsSet())
				{
					NewestSampleTime.Timecode = FQualifiedFrameTime(Sample->GetTimecode().GetValue(), EvaluationSettings.FrameRate);
				}
			}
		}

		//Update statistics about evaluation time
		if (IsBufferStatsEnabled())
		{
			const double EvaluationInSeconds = PlayerTime.GetTotalSeconds();
			if (EvaluationSettings.EvaluationType == ETimedDataInputEvaluationType::Timecode)
			{
				//Compute the distance with Timespan resolution. Goin through FQualifiedFrameTime gives a different result (~10ns)
				const double NewestSampleInSeconds = Samples[0]->GetTimecode().GetValue().ToTimespan(EvaluationSettings.FrameRate).GetTotalSeconds();
				const double OldestSampleInSeconds = Samples[CachedSamplesData.Num() - 1]->GetTimecode().GetValue().ToTimespan(EvaluationSettings.FrameRate).GetTotalSeconds();
				CachedEvaluationData.DistanceToNewestSampleSeconds = NewestSampleInSeconds - EvaluationInSeconds;
				CachedEvaluationData.DistanceToOldestSampleSeconds = EvaluationInSeconds - OldestSampleInSeconds;
			}
			else //Platform time
			{
				CachedEvaluationData.DistanceToNewestSampleSeconds = Samples[0]->GetTime().GetTotalSeconds() - EvaluationInSeconds;
				CachedEvaluationData.DistanceToOldestSampleSeconds = EvaluationInSeconds - Samples[CachedSamplesData.Num() - 1]->GetTime().GetTotalSeconds();
			}

			if (!FMath::IsNearlyZero(CachedEvaluationData.DistanceToNewestSampleSeconds) && CachedEvaluationData.DistanceToNewestSampleSeconds < 0.0f)
			{
				++BufferOverflow;
			}

			if (!FMath::IsNearlyZero(CachedEvaluationData.DistanceToOldestSampleSeconds) && CachedEvaluationData.DistanceToOldestSampleSeconds < 0.0f)
			{
				++BufferUnderflow;
			}
		}
	}
}

template<typename SampleType>
FText FMediaIOCoreSampleContainer<SampleType>::GetDisplayName() const
{
	return FText::FromName(ChannelName);
}

template<typename SampleType>
void FMediaIOCoreSampleContainer<SampleType>::ResetBufferStats()
{
	BufferUnderflow = 0;
	BufferOverflow = 0;
	FrameDrop = 0;
	CachedEvaluationData = FTimedDataInputEvaluationData();
}

template<typename SampleType>
void FMediaIOCoreSampleContainer<SampleType>::GetLastEvaluationData(FTimedDataInputEvaluationData& OutEvaluationData) const
{
	OutEvaluationData = CachedEvaluationData;
}

template<typename SampleType>
int32 FMediaIOCoreSampleContainer<SampleType>::GetFrameDroppedStat() const
{
	return FrameDrop;
}

template<typename SampleType>
int32 FMediaIOCoreSampleContainer<SampleType>::GetBufferOverflowStat() const
{
	return BufferOverflow;
}

template<typename SampleType>
int32 FMediaIOCoreSampleContainer<SampleType>::GetBufferUnderflowStat() const
{
	return BufferUnderflow;
}

template<typename SampleType>
void FMediaIOCoreSampleContainer<SampleType>::SetBufferStatsEnabled(bool bEnable)
{
	if (bEnable && !bIsStatEnabled)
	{
		//When enabling stat tracking, start clean
		ResetBufferStats();
	}

	bIsStatEnabled = bEnable;
}

template<typename SampleType>
bool FMediaIOCoreSampleContainer<SampleType>::IsBufferStatsEnabled() const
{
	return bIsStatEnabled;
}

template<typename SampleType>
void FMediaIOCoreSampleContainer<SampleType>::SetDataBufferSize(int32 BufferSize)
{
	EvaluationSettings.BufferSize = FMath::Clamp(BufferSize, 1, EvaluationSettings.AbsoluteMaxBufferSize);
}

template<typename SampleType>
int32 FMediaIOCoreSampleContainer<SampleType>::GetNumberOfSamples() const
{
	return CachedSamplesData.Num();
}

template<typename SampleType>
TArray<FTimedDataChannelSampleTime> FMediaIOCoreSampleContainer<SampleType>::GetDataTimes() const
{
	return CachedSamplesData;
}

template<typename SampleType>
FTimedDataChannelSampleTime FMediaIOCoreSampleContainer<SampleType>::GetNewestDataTime() const
{
	if (CachedSamplesData.Num())
	{
		return CachedSamplesData[CachedSamplesData.Num() - 1];
	}

	return FTimedDataChannelSampleTime();
}

template<typename SampleType>
FTimedDataChannelSampleTime FMediaIOCoreSampleContainer<SampleType>::GetOldestDataTime() const
{
	if (CachedSamplesData.Num())
	{
		return CachedSamplesData[0];
	}

	return FTimedDataChannelSampleTime();
}

template<typename SampleType>
ETimedDataInputState FMediaIOCoreSampleContainer<SampleType>::GetState() const
{
	return ETimedDataInputState::Connected;
}

template<typename SampleType>
int32 FMediaIOCoreSampleContainer<SampleType>::GetDataBufferSize() const
{
	return EvaluationSettings.BufferSize;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ITimedDataInput.h"

#include "CoreMinimal.h"
#include "IMediaSamples.h"
#include "IMediaTextureSample.h"
#include "ITimeManagementModule.h"
#include "MediaObjectPool.h"
#include "Misc/ScopeLock.h"
#include "Templates/SharedPointer.h"
#include "TimedDataInputCollection.h"


struct FMediaIOSamplingSettings
{
	FMediaIOSamplingSettings()
		: FrameRate(24, 1)
	{}

	FFrameRate FrameRate;
	ETimedDataInputEvaluationType EvaluationType = ETimedDataInputEvaluationType::PlatformTime;
	int32 BufferSize = 8;
	double PlayerTimeOffset = 0.0;
	int32 AbsoluteMaxBufferSize = 32;
};

/**
 * MediaIO container for different types of samples. Also a TimedData channel that can be monitored
 */
template<typename SampleType>
class MEDIAIOCORE_API FMediaIOCoreSampleContainer : public ITimedDataInputChannel
{
public:
	FMediaIOCoreSampleContainer(ITimedDataInput* Owner, FName InChannelName)
		: Input(Owner)
		, ChannelName(InChannelName)
		, BufferUnderflow(0)
		, BufferOverflow(0)
		, FrameDrop(0)
		, bIsStatEnabled(true)
		, bIsChannelEnabled(false)
	{
	}

	FMediaIOCoreSampleContainer(const FMediaIOCoreSampleContainer&) = delete;
	FMediaIOCoreSampleContainer& operator=(const FMediaIOCoreSampleContainer&) = delete;
	virtual ~FMediaIOCoreSampleContainer() = default;

public:
	/** Update this sample container settings */
	void UpdateSettings(const FMediaIOSamplingSettings& InSettings);

	/** Caches the current sample container states before samples will be taken out of it */
	void CacheState(FTimespan PlayerTime);

	/** Channel is disabled by default. It won't be added to the Timed Data collection if not enabled */
	void EnableChannel(bool bShouldEnable)
	{
		if (bShouldEnable != bIsChannelEnabled)
		{
			bIsChannelEnabled = bShouldEnable;

			if (bIsChannelEnabled)
			{
				if (Input)
				{
					Input->AddChannel(this);
				}
				ITimeManagementModule::Get().GetTimedDataInputCollection().Add(this);
			}
			else
			{
				ITimeManagementModule::Get().GetTimedDataInputCollection().Remove(this);
				if (Input)
				{
					Input->RemoveChannel(this);
				}
			}
		}
	}

public:

	/**
	 * Add the given sample to the cache.
	 *
	 * @param Sample The sample to add.
	 * @return True if the operation succeeds.
	 */
	bool AddSample(const TSharedRef<SampleType, ESPMode::ThreadSafe>& Sample)
	{
		FScopeLock Lock(&CriticalSection);

		const int32 FutureSize = Samples.Num() + 1;
		if (FutureSize > EvaluationSettings.BufferSize)
		{
			++FrameDrop;
			Samples.RemoveAt(Samples.Num() - 1);
		}

		Samples.EmplaceAt(0, Sample);

		return true;
	}

	/**
	 * Pop a sample from the cache.
	 *
	 * @return True if the operation succeeds.
	 * @see AddSample, NumSamples
	 */
	bool PopSample()
	{
		FScopeLock Lock(&CriticalSection);
		const int32 SampleCount = Samples.Num();
		if (SampleCount > 0)
		{
			Samples.RemoveAt(SampleCount - 1);
			return true;
		}
		return false;
	}
	
	/**
	 * Get the number of queued samples.
	 *
	 * @return Number of samples.
	 * @see AddSample, PopSample
	 */
	int32 NumSamples() const
	{
		FScopeLock Lock(&CriticalSection);
		return Samples.Num();
	}

	/**
	 * Get next sample time from the sample list.
	 *
	 * @return Time of the next sample from the VideoSampleQueue
	 * @see AddSample, NumSamples
	 */
	FTimespan GetNextSampleTime()
	{
		FScopeLock Lock(&CriticalSection);

		const int32 SampleCount = Samples.Num();
		if (SampleCount > 0)
		{
			TSharedPtr<SampleType, ESPMode::ThreadSafe> Sample = Samples[SampleCount - 1];
			if (Sample.IsValid())
			{
				return Sample->GetTime();
			}
		}

		return FTimespan();
	}

public:
	//~ Begin ITimedDataInputChannel
	virtual FText GetDisplayName() const override;
	virtual ETimedDataInputState GetState() const override;
	virtual FTimedDataChannelSampleTime GetOldestDataTime() const override;
	virtual FTimedDataChannelSampleTime GetNewestDataTime() const override;
	virtual TArray<FTimedDataChannelSampleTime> GetDataTimes() const override;
	virtual int32 GetNumberOfSamples() const override;
	virtual int32 GetDataBufferSize() const override;
	virtual void SetDataBufferSize(int32 BufferSize) override;
	virtual bool IsBufferStatsEnabled() const override;
	virtual void SetBufferStatsEnabled(bool bEnable) override;
	virtual int32 GetBufferUnderflowStat() const override;
	virtual int32 GetBufferOverflowStat() const override;
	virtual int32 GetFrameDroppedStat() const override;
	virtual void GetLastEvaluationData(FTimedDataInputEvaluationData& OutEvaluationData) const override;
	virtual void ResetBufferStats() override;

public:

	bool FetchSample(TRange<FTimespan> TimeRange, TSharedPtr<SampleType, ESPMode::ThreadSafe>& OutSample);
	void FlushSamples();

protected:

	/** Input in which that channel belongs */
	ITimedDataInput* Input;

	/** Name of this channel */
	FName ChannelName;

	/** Samples are consumed by the the facade layer when evaluating and sending to render thread. Cache sample data before fetching happens */
	TArray<FTimedDataChannelSampleTime> CachedSamplesData;

	/** Last evaluation data for that channel */
	FTimedDataInputEvaluationData CachedEvaluationData;

	/** Evaluation statistics we keep track of */
	TAtomic<int32> BufferUnderflow;
	TAtomic<int32> BufferOverflow;
	TAtomic<int32> FrameDrop;

	/** Channel settings */
	FMediaIOSamplingSettings EvaluationSettings;

	/** Stats logging enabled or not */
	bool bIsStatEnabled;

	/** Should this channel be considered available */
	bool bIsChannelEnabled;

	/** 
	 * Sample container: We add at the beginning of the array [0] and we pop at the end [Size-1]. 
	 * [0] == Newest sample
	 * [Size - 1] == Oldest sample
	 */
	mutable FCriticalSection CriticalSection;
	TArray<TSharedPtr<SampleType, ESPMode::ThreadSafe>> Samples;
};





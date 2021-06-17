// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sound/QuartzQuantizationUtilities.h"

#include "Core/Public/CoreGlobals.h"
#include "Core/Public/Math/NumericLimits.h"
#include "Core/Public/Math/UnrealMathUtility.h"
#include "AudioMixerDevice.h"

#define INVALID_DURATION -1


DEFINE_LOG_CATEGORY(LogAudioQuartz);


FQuartzTimeSignature::FQuartzTimeSignature(const FQuartzTimeSignature& Other)
	: NumBeats(Other.NumBeats)
	, BeatType(Other.BeatType)
	, OptionalPulseOverride(Other.OptionalPulseOverride)
{
}

FQuartzTimeSignature& FQuartzTimeSignature::operator=(const FQuartzTimeSignature& Other)
{
	NumBeats = Other.NumBeats;
	BeatType = Other.BeatType;
	OptionalPulseOverride = Other.OptionalPulseOverride;

	return *this;
}

bool FQuartzTimeSignature::operator==(const FQuartzTimeSignature& Other)
{
	bool Result = (NumBeats == Other.NumBeats);
	Result &= (BeatType == Other.BeatType);
	Result &= (OptionalPulseOverride.Num() == Other.OptionalPulseOverride.Num());

	const int32 NumPulseEntries = OptionalPulseOverride.Num();


	if (Result && NumPulseEntries)
	{
		for (int32 i = 0; i < NumPulseEntries; ++i)
		{
			const bool NumPulsesMatch = (OptionalPulseOverride[i].NumberOfPulses == Other.OptionalPulseOverride[i].NumberOfPulses);
			const bool DurationsMatch = (OptionalPulseOverride[i].PulseDuration == Other.OptionalPulseOverride[i].PulseDuration);

			if (!(NumPulseEntries && DurationsMatch))
			{
				Result = false;
				break;
			}
		}
	}

	return Result;
}


FQuartLatencyTracker::FQuartLatencyTracker()
	: Min(TNumericLimits<float>::Max())
	, Max(TNumericLimits<float>::Min())
{
}

void FQuartLatencyTracker::PushLatencyTrackerResult(const double& InResult)
{
	ResultQueue.Enqueue((float)InResult);

	if (!IsInGameThread())
	{
		return;
	}

	DigestQueue();
}

float FQuartLatencyTracker::GetLifetimeAverageLatency()
{
	if (IsInGameThread())
	{
		DigestQueue();
	}

	return LifetimeAverage;
}

float FQuartLatencyTracker::GetMinLatency()
{
	if (IsInGameThread())
	{
		DigestQueue();
	}

	return Min;
}

float FQuartLatencyTracker::GetMaxLatency()
{
	if (IsInGameThread())
	{
		DigestQueue();
	}

	return Max;
}

void FQuartLatencyTracker::PushSingleResult(const double& InResult)
{
	if (++NumEntries == 0)
	{
		LifetimeAverage = InResult;
	}
	else
	{
		LifetimeAverage = (LifetimeAverage * (NumEntries - 1) + InResult) / NumEntries;
	}

	Min = FMath::Min(Min, static_cast<float>(InResult));
	Max = FMath::Max(Max, static_cast<float>(InResult));
}

void FQuartLatencyTracker::DigestQueue()
{
	check(IsInGameThread());

	float Result;
	while (ResultQueue.Dequeue(Result))
	{
		PushSingleResult(Result);
	}
}



namespace Audio
{
	FQuartzClockTickRate::FQuartzClockTickRate()
	{
		SetBeatsPerMinute(60.f);
	}

	void FQuartzClockTickRate::SetFramesPerTick(int32 InNewFramesPerTick)
	{
		if (InNewFramesPerTick < 1)
		{
			UE_LOG(LogAudioQuartz, Warning, TEXT("Quartz Metronme requires at least 1 frame per tick, clamping request"));
			InNewFramesPerTick = 1;
		}

		FramesPerTick = InNewFramesPerTick;
		RecalculateDurationsBasedOnFramesPerTick();
	}

	void FQuartzClockTickRate::SetMillisecondsPerTick(float InNewMillisecondsPerTick)
	{
		FramesPerTick = FMath::Max(1.0f, (InNewMillisecondsPerTick * SampleRate) / 1000.f);
		RecalculateDurationsBasedOnFramesPerTick();
	}

	void FQuartzClockTickRate::SetThirtySecondNotesPerMinute(float InNewThirtySecondNotesPerMinute)
	{
		check(InNewThirtySecondNotesPerMinute > 0);

		FramesPerTick = FMath::Max(1.0f, (60.f * SampleRate ) / InNewThirtySecondNotesPerMinute);
		RecalculateDurationsBasedOnFramesPerTick();
	}

	void FQuartzClockTickRate::SetBeatsPerMinute(float InNewBeatsPerMinute)
	{
		// same as 1/32nd notes,
		// except there are 1/8th the number of quarter notes than thirty-second notes in a minute
		// (So FramesPerTick should be 8 times shorter than it was when setting 32nd notes)

		// FramesPerTick = 1/8 * (60.f / (InNewBeatsPerMinute)) * SampleRate;
		// (60.0 / 8.0) = 7.5f

		FramesPerTick = FMath::Max(1.0f, (7.5f * SampleRate) / InNewBeatsPerMinute);
		RecalculateDurationsBasedOnFramesPerTick();
	}

	void FQuartzClockTickRate::SetSampleRate(float InNewSampleRate)
	{
		check(InNewSampleRate >= 0);

		FramesPerTick = FMath::Max(1.0f, (InNewSampleRate / SampleRate) * static_cast<float>(FramesPerTick));
		SampleRate = InNewSampleRate;

		RecalculateDurationsBasedOnFramesPerTick();
	}

	int64 FQuartzClockTickRate::GetFramesPerDuration(EQuartzCommandQuantization InDuration) const
	{
		const int64 FramesPerDotted16th = FramesPerTick * 3;
		const int64 FramesPer16thTriplet = 4.f * FramesPerTick / 3.f;

		switch (InDuration)
		{
		case EQuartzCommandQuantization::None:
			return 0;

			// NORMAL
		case EQuartzCommandQuantization::Tick:
		case EQuartzCommandQuantization::ThirtySecondNote:
			return FramesPerTick; // same as 1/32nd note

		case EQuartzCommandQuantization::SixteenthNote:
			return (int64)FramesPerTick << 1;

		case EQuartzCommandQuantization::EighthNote:
			return (int64)FramesPerTick << 2;

		case EQuartzCommandQuantization::Beat: // default to quarter note (should be overridden for non-basic meters)
		case EQuartzCommandQuantization::QuarterNote:
			return (int64)FramesPerTick << 3;

		case EQuartzCommandQuantization::HalfNote:
			return (int64)FramesPerTick << 4;

		case EQuartzCommandQuantization::Bar: // default to whole note (should be overridden for non-4/4 meters)
		case EQuartzCommandQuantization::WholeNote:
			return (int64)FramesPerTick << 5;

			// DOTTED
		case EQuartzCommandQuantization::DottedSixteenthNote:
			return FramesPerDotted16th;

		case EQuartzCommandQuantization::DottedEighthNote:
			return FramesPerDotted16th << 1;

		case EQuartzCommandQuantization::DottedQuarterNote:
			return FramesPerDotted16th << 2;

		case EQuartzCommandQuantization::DottedHalfNote:
			return FramesPerDotted16th << 3;

		case EQuartzCommandQuantization::DottedWholeNote:
			return FramesPerDotted16th << 4;


			// TRIPLETS
		case EQuartzCommandQuantization::SixteenthNoteTriplet:
			return FramesPer16thTriplet;

		case EQuartzCommandQuantization::EighthNoteTriplet:
			return FramesPer16thTriplet << 1;

		case EQuartzCommandQuantization::QuarterNoteTriplet:
			return FramesPer16thTriplet << 2;

		case EQuartzCommandQuantization::HalfNoteTriplet:
			return FramesPer16thTriplet << 3;



		default:
			checkf(false, TEXT("Unexpected EAudioMixerCommandQuantization: Need to update switch statement for new quantization enumeration?"));
			break;
		}

		return INVALID_DURATION;
	}

	int64 FQuartzClockTickRate::GetFramesPerDuration(EQuartzTimeSignatureQuantization InDuration) const
	{
		switch (InDuration)
		{
		case EQuartzTimeSignatureQuantization::HalfNote:
			return GetFramesPerDuration(EQuartzCommandQuantization::HalfNote);

		case EQuartzTimeSignatureQuantization::QuarterNote:
			return GetFramesPerDuration(EQuartzCommandQuantization::QuarterNote);

		case EQuartzTimeSignatureQuantization::EighthNote:
			return GetFramesPerDuration(EQuartzCommandQuantization::EighthNote);

		case EQuartzTimeSignatureQuantization::SixteenthNote:
			return GetFramesPerDuration(EQuartzCommandQuantization::SixteenthNote);

		case EQuartzTimeSignatureQuantization::ThirtySecondNote:
			return GetFramesPerDuration(EQuartzCommandQuantization::ThirtySecondNote);

		default:
			checkf(false, TEXT("Unexpected EQuartzTimeSignatureQuantization: Need to update switch statement for new quantization enumeration?"));
			break;
		}

		return INVALID_DURATION;
	}

	bool FQuartzClockTickRate::IsValid(int32 InEventResolutionThreshold) const
	{
		ensureMsgf(InEventResolutionThreshold > 0
			, TEXT("Querying a the validity of an FQuartzClockTickRate object w/ a zero or negative threshold of (%i)")
			, InEventResolutionThreshold);

		if (FramesPerTick < InEventResolutionThreshold)
		{
			return false;
		}

		return true;
	}

	bool FQuartzClockTickRate::IsSameTickRate(const FQuartzClockTickRate& Other, bool bAccountForDifferentSampleRates) const
	{
		if (!bAccountForDifferentSampleRates)
		{
			const bool Result = FramesPerTick == Other.FramesPerTick;

			// All other members SHOULD be equal if the FramesPerTick (ground truth) are equal
			checkSlow(!Result ||
				(FMath::IsNearlyEqual(MillisecondsPerTick, Other.MillisecondsPerTick)
					&& FMath::IsNearlyEqual(ThirtySecondNotesPerMinute, Other.ThirtySecondNotesPerMinute)
					&& FMath::IsNearlyEqual(BeatsPerMinute, Other.BeatsPerMinute)
					&& FMath::IsNearlyEqual(SampleRate, Other.SampleRate)));

			return Result;
		}
		else
		{
			// Perform SampleRate conversion on a temporary to see if
			FQuartzClockTickRate TempTickRate = Other;
			TempTickRate.SetSampleRate(SampleRate);

			const bool Result = FramesPerTick == TempTickRate.FramesPerTick;

			// All other members SHOULD be equal if the FramesPerTick (ground truth) are equal
			checkSlow(!Result ||
				(FMath::IsNearlyEqual(MillisecondsPerTick, TempTickRate.MillisecondsPerTick)
					&& FMath::IsNearlyEqual(ThirtySecondNotesPerMinute, TempTickRate.ThirtySecondNotesPerMinute)
					&& FMath::IsNearlyEqual(BeatsPerMinute, TempTickRate.BeatsPerMinute)
					&& FMath::IsNearlyEqual(SampleRate, TempTickRate.SampleRate)));

			return Result;
		}
	}

	void FQuartzClockTickRate::RecalculateDurationsBasedOnFramesPerTick()
	{
		check(FramesPerTick > 0);
		check(SampleRate > 0);
		const float FloatFramesPerTick = static_cast<float>(FramesPerTick);

		SecondsPerTick = (FloatFramesPerTick / SampleRate);
		MillisecondsPerTick = SecondsPerTick * 1000.f;
		ThirtySecondNotesPerMinute = (60.f * SampleRate) / FloatFramesPerTick;
		BeatsPerMinute = ThirtySecondNotesPerMinute / 8.0f;
	}


	void FQuartzClockTickRate::SetSecondsPerTick(float InNewSecondsPerTick)
	{
		SetMillisecondsPerTick(InNewSecondsPerTick * 1000.f);
	}


	FQuartzQuantizedCommandInitInfo::FQuartzQuantizedCommandInitInfo(
		const FQuartzQuantizedRequestData& RHS
		, int32 InSourceID
	)
		: ClockName(RHS.ClockName)
		, ClockHandleName(RHS.ClockHandleName)
		, OtherClockName(RHS.OtherClockName)
		, QuantizedCommandPtr(RHS.QuantizedCommandPtr)
		, QuantizationBoundary(RHS.QuantizationBoundary)
		, GameThreadCommandQueue(RHS.GameThreadCommandQueue)
		, GameThreadDelegateID(RHS.GameThreadDelegateID)
		, OwningClockPointer(nullptr)
		, SourceID(InSourceID)
	{
	}

	TSharedPtr<IQuartzQuantizedCommand> IQuartzQuantizedCommand::GetDeepCopyOfDerivedObject() const
	{
		// implement this method to allow copies to be made from pointers to base class
		checkSlow(false);
		return nullptr;
	}

	void IQuartzQuantizedCommand::OnQueued(const FQuartzQuantizedCommandInitInfo& InCommandInitInfo)
	{
		Audio::FMixerDevice* MixerDevice = InCommandInitInfo.OwningClockPointer->GetMixerDevice();
		if (MixerDevice)
		{
			MixerDevice->QuantizedEventClockManager.PushLatencyTrackerResult(FQuartzCrossThreadMessage::RequestRecieved());
		}

		GameThreadCommandQueue = InCommandInitInfo.GameThreadCommandQueue; 
		GameThreadDelegateID = InCommandInitInfo.GameThreadDelegateID;

		if (GameThreadCommandQueue.IsValid())
		{
			FQuartzQuantizedCommandDelegateData Data;

			Data.DelegateSubType = EQuartzCommandDelegateSubType::CommandOnQueued;
			Data.DelegateID = GameThreadDelegateID;

			// TODO: add payload to Data

			GameThreadCommandQueue->PushEvent(Data);
		}

		UE_LOG(LogAudioQuartz, Verbose, TEXT("OnQueued() called for quantized event type: [%s]"), *GetCommandName().ToString());
		OnQueuedCustom(InCommandInitInfo);
	}

	void IQuartzQuantizedCommand::FailedToQueue()
	{
		if (GameThreadCommandQueue.IsValid())
		{
			FQuartzQuantizedCommandDelegateData Data;
			Data.DelegateID = GameThreadDelegateID;

			// TODO: add payload to Data

			GameThreadCommandQueue->PushEvent(Data);
		}

		UE_LOG(LogAudioQuartz, Verbose, TEXT("FailedToQueue() called for quantized event type: [%s]"), *GetCommandName().ToString());
		FailedToQueueCustom();
	}

	void IQuartzQuantizedCommand::AboutToStart()
	{
		// only call once for the lifespan of this event
		if (bAboutToStartHasBeenCalled)
		{
			return;
		}

		bAboutToStartHasBeenCalled = true;


		if (GameThreadCommandQueue.IsValid())
		{
			FQuartzQuantizedCommandDelegateData Data;

			Data.DelegateSubType = EQuartzCommandDelegateSubType::CommandOnAboutToStart;
			Data.DelegateID = GameThreadDelegateID;

			// TODO: add payload to Data

			GameThreadCommandQueue->PushEvent(Data);
		}

		UE_LOG(LogAudioQuartz, Verbose, TEXT("AboutToStart() called for quantized event type: [%s]"), *GetCommandName().ToString());
		AboutToStartCustom();
	}

	void IQuartzQuantizedCommand::OnFinalCallback(int32 InNumFramesLeft)
	{
		if (GameThreadCommandQueue.IsValid())
		{
			FQuartzQuantizedCommandDelegateData OnStartedData;

			OnStartedData.DelegateSubType = EQuartzCommandDelegateSubType::CommandOnStarted;
			OnStartedData.DelegateID = GameThreadDelegateID;

			// TODO: add payload to Data

			GameThreadCommandQueue->PushEvent(OnStartedData);

			// 			if (!IsLooping())
			// 			{
			// 				FQuartzQuantizedCommandDelegateData CompletedData;
			// 				CompletedData.DelegateSubType = EQuartzCommandDelegateSubType::CommandCompleted;
			// 				CompletedData.DelegateID = GameThreadDelegateID;
			// 
			// 				GameThreadCommandQueue->PushEvent(CompletedData);
			// 			}
		}

		UE_LOG(LogAudioQuartz, Verbose, TEXT("OnFinalCallback() called for quantized event type: [%s]"), *GetCommandName().ToString());
		OnFinalCallbackCustom(InNumFramesLeft);
	}

	void IQuartzQuantizedCommand::OnClockPaused()
	{
		UE_LOG(LogAudioQuartz, Verbose, TEXT("OnClockPaused() called for quantized event type: [%s]"), *GetCommandName().ToString());
		OnClockPausedCustom();
	}

	void IQuartzQuantizedCommand::OnClockStarted()
	{
		UE_LOG(LogAudioQuartz, Verbose, TEXT("OnClockStarted() called for quantized event type: [%s]"), *GetCommandName().ToString());
		OnClockStartedCustom();
	}

	void IQuartzQuantizedCommand::Cancel()
	{
		if (GameThreadCommandQueue.IsValid())
		{
			FQuartzQuantizedCommandDelegateData Data;

			Data.DelegateSubType = EQuartzCommandDelegateSubType::CommandOnCanceled;
			Data.DelegateID = GameThreadDelegateID;

			// TODO: add payload to Data

			GameThreadCommandQueue->PushEvent(Data);
		}

		UE_LOG(LogAudioQuartz, Verbose, TEXT("Cancel() called for quantized event type: [%s]"), *GetCommandName().ToString());
		CancelCustom();
	}



	bool FQuartzQuantizedCommandHandle::Cancel()
	{
		checkSlow(MixerDevice);
		checkSlow(MixerDevice->IsAudioRenderingThread());

		if (CommandPtr && MixerDevice && !OwningClockName.IsNone())
		{
			UE_LOG(LogAudioQuartz, Verbose, TEXT("OnQueued() called for quantized event type: [%s]"), *CommandPtr->GetCommandName().ToString());
			return MixerDevice->QuantizedEventClockManager.CancelCommandOnClock(OwningClockName, CommandPtr);
		}

		return false;
	}


	FQuartzLatencyTimer::FQuartzLatencyTimer()
		: JourneyStartCycles(-1)
		, JourneyEndCycles(-1)
	{
	}

	void FQuartzLatencyTimer::StartTimer()
	{
		JourneyStartCycles = FPlatformTime::Cycles64();
	}

	void FQuartzLatencyTimer::ResetTimer()
	{
		JourneyStartCycles = -1;
		JourneyEndCycles = -1;
	}

	void FQuartzLatencyTimer::StopTimer()
	{
		JourneyEndCycles = FPlatformTime::Cycles64();

	}

	double FQuartzLatencyTimer::GetCurrentTimePassedMs()
	{
		if (!IsTimerRunning())
		{
			return 0.0;
		}

		return FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - JourneyStartCycles);
	}

	double FQuartzLatencyTimer::GetResultsMilliseconds()
	{
		if (HasTimerRun())
		{
			return FPlatformTime::ToMilliseconds64(JourneyEndCycles - JourneyStartCycles);
		}

		return 0.0;
	}

	bool FQuartzLatencyTimer::HasTimerStarted()
	{
		return JourneyStartCycles > 0;
	}

	bool FQuartzLatencyTimer::HasTimerStopped()
	{
		return JourneyEndCycles > 0;
	}

	bool FQuartzLatencyTimer::IsTimerRunning()
	{
		return HasTimerStarted() && !HasTimerStopped();
	}

	bool FQuartzLatencyTimer::HasTimerRun()
	{
		return HasTimerStarted() && HasTimerStopped();
	}

	FQuartzCrossThreadMessage::FQuartzCrossThreadMessage(bool bAutoStartTimer)
	{
		if (bAutoStartTimer)
		{
			Timer.StartTimer();
		}
	}

	void FQuartzCrossThreadMessage::RequestSent()
	{
		Timer.StartTimer();
	}

	double FQuartzCrossThreadMessage::RequestRecieved()
	{
		Timer.StopTimer();
		return GetResultsMilliseconds();
	}

	double FQuartzCrossThreadMessage::GetResultsMilliseconds()
	{
		return Timer.GetResultsMilliseconds();
	}

	double FQuartzCrossThreadMessage::GetCurrentTimeMilliseconds()
	{
		return Timer.GetCurrentTimePassedMs();
	}


} // namespace Audio

bool FQuartzTransportTimeStamp::IsZero() const
{
	return (!Bars) && (!Beat) && FMath::IsNearlyZero(BeatFraction);
}

void FQuartzTransportTimeStamp::Reset()
{
	Bars = 0;
	Beat = 0;
	BeatFraction = 0.f;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sound/QuartzQuantizationUtilities.h"

namespace Audio
{
	using MetronomeCommandQueuePtr = TSharedPtr<FShareableQuartzCommandQueue, ESPMode::ThreadSafe>;

	// Class to track the passage of musical time, and allow subscribers to be notified when these musical events take place
	class FQuartzMetronome
	{
	public:
		// ctor
		FQuartzMetronome();
		FQuartzMetronome(const FQuartzTimeSignature& InTimeSignature);

		// dtor
		~FQuartzMetronome();

		// called by owning FQuartzClock
		void Tick(int32 InNumSamples, int32 FramesOfLatency = 0);

		// called by owning FQuartzClock
		void SetTickRate(FQuartzClockTickRate InNewTickRate, int32 NumFramesLeft = 0);

		FQuartzClockTickRate GetTickRate() const { return CurrentTickRate; }

		void SetSampleRate(float InNewSampleRate);

		// affects bars/beats values we send back to the game thread
		void SetTimeSignature(const FQuartzTimeSignature& InNewTimeSignature);

		double GetFramesUntilBoundary(FQuartzQuantizationBoundary InQuantizationBoundary) const;

		const FQuartzTimeSignature & GetTimeSignature() const { return CurrentTimeSignature; }

		FQuartzTransportTimeStamp GetTimeStamp() const { return CurrentTimeStamp; }

		double GetTimeSinceStart() const { return TimeSinceStart; }

		void SubscribeToTimeDivision(MetronomeCommandQueuePtr InListenerQueue, EQuartzCommandQuantization InQuantizationBoundary);

		void SubscribeToAllTimeDivisions(MetronomeCommandQueuePtr InListenerQueue);

		void UnsubscribeFromTimeDivision(MetronomeCommandQueuePtr InListenerQueue, EQuartzCommandQuantization InQuantizationBoundary);

		void UnsubscribeFromAllTimeDivisions(MetronomeCommandQueuePtr InListenerQueue);

		void ResetTransport();

	private:
		void RecalculateDurations();

		void FireEvents(int32 EventFlags);

		float CountNumSubdivisionsPerBar(EQuartzCommandQuantization InSubdivision) const;

		float CountNumSubdivisionsSinceBarStart(EQuartzCommandQuantization InSubdivision) const;

		float CountNumSubdivisionsSinceStart(EQuartzCommandQuantization InSubdivision) const;

		int32 ListenerFlags{ 0 };

		FQuartzTransportTimeStamp CurrentTimeStamp;

		FQuartzTimeSignature CurrentTimeSignature;

		FQuartzClockTickRate CurrentTickRate;

		// (Subtract one because Tick and 1/32 are the same.)
		TArray<MetronomeCommandQueuePtr> MetronomeSubscriptionMatrix[static_cast<int32>(EQuartzCommandQuantization::Count)];
		
		// wrapper around our array so it can be indexed into by different Enums that represent musical time
		struct FramesInTimeValue
		{
		public:
			// index operators for EQuartzCommandQuantization
			double& operator[](EQuartzCommandQuantization InTimeValue)
			{
				return FramesInTimeValueInternal[static_cast<int32>(InTimeValue)];
			}

			const double& operator[](EQuartzCommandQuantization InTimeValue) const
			{
				return FramesInTimeValueInternal[static_cast<int32>(InTimeValue)];
			}

			// index operators for int32
			double& operator[](int32 Index)
			{
				return FramesInTimeValueInternal[Index];
			}

			const double& operator[](int32 Index) const
			{
				return FramesInTimeValueInternal[Index];
			}

			double FramesInTimeValueInternal[static_cast<int32>(EQuartzCommandQuantization::Count)]{ 0.0 };
		};

		// array of lengths of musical durations (in audio frames)
		FramesInTimeValue MusicalDurationsInFrames;

		// array of the number of audio frames left until the respective musical duration
		FramesInTimeValue FramesLeftInMusicalDuration;

		// optional array of pulse duration overrides (for odd meters)
		TArray<double> PulseDurations;

		// the index of the active pulse duration override
		int32 PulseDurationIndex{ -1 };

		int32 LastFramesOfLatency{ 0 };

		//Keeps track of time in seconds since the Clock was last reset
		double TimeSinceStart;

	}; // class QuartzMetronome
} // namespace Audio
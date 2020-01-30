// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkedSimulationModelTime.h"

// Holds per-simulation settings about how ticking is supposed to happen.
template<int32 InFixedStepMS=0, int32 InMaxStepMS=0>
struct TNetworkSimTickSettings
{
	static_assert(!(InFixedStepMS!=0 && InMaxStepMS != 0), "MaxStepMS is only applicable when using variable step (FixedStepMS == 0)");
	enum 
	{
		MaxStepMS = InMaxStepMS,				// Max step. Only applicable to variable time step.
		FixedStepMS = InFixedStepMS,			// Fixed step. If 0, then we are "variable time step"
	};

	// Typed accessors
	static constexpr FNetworkSimTime::FSimTime GetMaxStepMS() { return static_cast<FNetworkSimTime::FSimTime>(InMaxStepMS); }
	static constexpr FNetworkSimTime::FSimTime GetFixedStepMS() { return static_cast<FNetworkSimTime::FSimTime>(InFixedStepMS); }
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	Accumulator: Helper for accumulating real time into sim time based on TickSettings
// ----------------------------------------------------------------------------------------------------------------------------------------------

template<typename TickSettings, bool IsFixedTick=(TickSettings::FixedStepMS!=0)>
struct TRealTimeAccumulator
{
	using TRealTime = FNetworkSimTime::FRealTime;
	void Accumulate(FNetworkSimTime& NetworkSimTime, const TRealTime RealTimeSeconds)
	{
		// Even though we are variable tick, we still want to truncate down to an even msec. This keeps sim steps as whole integer values that serialize better, don't have denormals or other floating point weirdness.
		// (If we ever wanted to just fully let floats pass through and be used as the msec sim time, this could be done through another specialization for float/float time).		
		// Also note that MaxStepMS enforcement does NOT belong here. Dropping time due to MaxStepMS would just make the sim run slower. MaxStepMS is used at the input processing level.
		
		AccumulatedTimeMS += RealTimeSeconds * FNetworkSimTime::GetRealToSimFactor(); // convert input seconds -> msec
		const FNetworkSimTime AccumulatedSimTimeMS = FNetworkSimTime::FromRealTimeMS(AccumulatedTimeMS);	// truncate (float) accumulated msec to (int32) sim time msec

		NetworkSimTime += AccumulatedSimTimeMS;
		AccumulatedTimeMS -= AccumulatedSimTimeMS.ToRealTimeMS(); // subtract out the "whole" msec, we are left with the remainder msec
	}

	void Reset()
	{
		AccumulatedTimeMS = 0.f;
	}

private:

	TRealTime AccumulatedTimeMS = 0.f;
};

// Specialized version of FixedTicking. This accumulates real time that spills over into NetworkSimTime as it crosses the FixStep threshold
template<typename TickSettings>
struct TRealTimeAccumulator<TickSettings, true>
{
	using TRealTime = FNetworkSimTime::FRealTime;
	const TRealTime RealTimeFixedStep = static_cast<TRealTime>(TickSettings::GetFixedStepMS() * FNetworkSimTime::GetSimToRealFactor());

	void Accumulate(FNetworkSimTime& NetworkSimTime, const TRealTime RealTimeSeconds)
	{
		AccumulatedTime += RealTimeSeconds;
		if (AccumulatedTime > RealTimeFixedStep)
		{
			const int32 NumFrames = AccumulatedTime / RealTimeFixedStep;
			AccumulatedTime -= NumFrames * RealTimeFixedStep;
				
			if (FMath::Abs<TRealTime>(AccumulatedTime) < SMALL_NUMBER)
			{
				AccumulatedTime = TRealTime(0.f);
			}

			NetworkSimTime += FNetworkSimTime::FromMSec(NumFrames * TickSettings::FixedStepMS);
		}
	}

	void Reset()
	{
		AccumulatedTime = 0.f;
	}

private:

	TRealTime AccumulatedTime;
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	FSimulationTickState: Holds active state for simulation ticking. We track two things: frames and time.
//
//	PendingFrame is the next frame we will process: the input/sync/aux state @ PendingFrame will be run through ::SimulationTick and produce the 
//	next frame's (PendingFrame+1) Sync and possibly Aux state (if it changes). "Out of band" modifications to the sync/aux state should happen
//	to PendingFrame (e.g, before it is processed. Once a frame has been processed, we won't run it through ::SimulationTick again!).
//
//	MaxAllowedFrame is a frame based limiter on simulation updates. This must be incremented to allow the simulation to advance.
//
//	Time is also tracked. We keep running total for how much the sim has advanced and how much it is allowed to advance. There is also a historic buffer of
//	simulation time in SimulationTimeBuffer.
//
//	Consider that Frames are essentially client dependent and gaps can happen due to packet loss, etc. Time will always be continuous though.
//	
// ----------------------------------------------------------------------------------------------------------------------------------------------

struct FSimulationTickState
{
	int32 PendingFrame = 0;
	int32 MaxAllowedFrame = -1;
	bool bUpdateInProgress = false;
	
	FNetworkSimTime GetTotalProcessedSimulationTime() const 
	{ 
		return TotalProcessedSimulationTime; 
	}

	void SetTotalProcessedSimulationTime(const FNetworkSimTime& SimTime, int32 Frame)
	{
		TotalProcessedSimulationTime = SimTime;
		*SimulationTimeBuffer.WriteFrame(Frame) = SimTime;
	}

	void IncrementTotalProcessedSimulationTime(const FNetworkSimTime& DeltaSimTime, int32 Frame)
	{
		TotalProcessedSimulationTime += DeltaSimTime;
		*SimulationTimeBuffer.WriteFrame(Frame) = TotalProcessedSimulationTime;
	}
		
	// Historic tracking of simulation time. This allows us to timestamp sync data as its produced
	TNetworkSimContiguousBuffer<FNetworkSimTime>	SimulationTimeBuffer;

	// How much granted simulation time is left to process
	FNetworkSimTime GetRemainingAllowedSimulationTime() const
	{
		return TotalAllowedSimulationTime - TotalProcessedSimulationTime;
	}

	FNetworkSimTime GetTotalAllowedSimulationTime() const
	{
		return TotalAllowedSimulationTime;
	}

protected:

	FNetworkSimTime TotalAllowedSimulationTime;	// Total time we have been "given" to process. We cannot process more simulation time than this: doing so would be speed hacking.
	FNetworkSimTime TotalProcessedSimulationTime;	// How much time we've actually processed. The only way to increment this is to process user commands or receive authoritative state from the network.
};

// "Ticker" that actually allows us to give the simulation time. This struct will generally not be passed around outside of the core TNetworkedSimulationModel/Replicators
template<typename TickSettings=TNetworkSimTickSettings<>>
struct TSimulationTicker : public FSimulationTickState
{
	using TSettings = TickSettings;

	void SetTotalAllowedSimulationTime(const FNetworkSimTime& SimTime)
	{
		TotalAllowedSimulationTime = SimTime;
		RealtimeAccumulator.Reset();
	}

	// "Grants" allowed simulation time to this tick state. That is, we are now allowed to advance the simulation by this amount the next time the sim ticks.
	// Note the input is RealTime in SECONDS. This is what the rest of the engine uses when dealing with float delta time.
	void GiveSimulationTime(float RealTimeSeconds)
	{
		RealtimeAccumulator.Accumulate(TotalAllowedSimulationTime, RealTimeSeconds);
	}	

private:

	TRealTimeAccumulator<TSettings>	RealtimeAccumulator;	
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	FNetSimTimeStep Time data structure that is passed into SimulationTick
// ----------------------------------------------------------------------------------------------------------------------------------------------

struct FNetSimTimeStep
{
	// The delta time step for this tick (in MS by default)
	const FNetworkSimTime& StepMS;
	// The tick state of the simulation prior to running this tick. E.g, does not "include" the above StepMS that we are simulating now.
	// The first time ::SimulationTick runs, TickState.GetTotalProcessedSimulationTime() == 0.
	const FSimulationTickState& TickState;
};
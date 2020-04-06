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
//	FNetSimTimeStep Time data structure that is passed into SimulationTick
// ----------------------------------------------------------------------------------------------------------------------------------------------

struct FNetSimTimeStep
{
	// The delta time step for this tick (in MS by default)
	const FNetworkSimTime& StepMS;

	// How much simulation time has ran up until this point.
	// This will be 0 the first time ::SimulationTick runs.
	const FNetworkSimTime& TotalSimulationTime;

	// The Simulation Frame number we are generating in this tick, E.g, the output frame.
	// This will be 1 the first time ::SimulationTick runs. (0 is the starting input and is not generated in a Tick)
	const int32& Frame;
};
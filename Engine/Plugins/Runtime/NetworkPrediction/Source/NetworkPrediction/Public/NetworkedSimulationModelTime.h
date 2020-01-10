// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkedSimulationModelBuffer.h"

// ----------------------------------------------------------------------------------------------------------------------------------------------
//		Tick and time keeping related structures
// ----------------------------------------------------------------------------------------------------------------------------------------------

// The main Simulation time type. All sims use this to talk about time.
struct NETWORKPREDICTION_API FNetworkSimTime
{
	using FSimTime = int32; // Underlying type used to store simulation time
	using FRealTime = float; // Underlying type used when dealing with realtime (usually coming in from the engine tick).

	enum
	{
		RealToSimFactor = 1000 // Factor to go from RealTime (always seconds) to SimTime (MSec by default with factor of 1000)
	};

	static constexpr FRealTime GetRealToSimFactor() { return static_cast<FRealTime>(RealToSimFactor); }
	static constexpr FRealTime GetSimToRealFactor() { return static_cast<FRealTime>(1.f / RealToSimFactor); }

	// ---------------------------------------------------------------

	FNetworkSimTime() { }

	// Things get confusing with templated types and overloaded functions. To avoid that, use these funcs to construct from either msec or real time
	static constexpr FNetworkSimTime FromMSec(const FSimTime& InTime) { return FNetworkSimTime(InTime); } 
	static constexpr FNetworkSimTime FromRealTimeSeconds(const FRealTime& InRealTime) { return FNetworkSimTime( static_cast<FSimTime>(InRealTime * GetRealToSimFactor())); } 
	FRealTime ToRealTimeSeconds() const { return (Time * GetSimToRealFactor()); }

	// Direct casts to "real time MS" which should be rarely used in practice (TRealTimeAccumulator only current case). All other cases of "real time" imply seconds.
	static constexpr FNetworkSimTime FromRealTimeMS(const FRealTime& InRealTime) { return FNetworkSimTime( static_cast<FSimTime>(InRealTime)); }
	
	FRealTime ToRealTimeMS() const { return static_cast<FRealTime>(Time); }
	FString ToString() const { return LexToString(this->Time); }

	bool IsPositive() const { return (Time > 0); }
	bool IsNegative() const { return (Time < 0); }
	void Reset() { Time = 0; }

	// FIXME
	void NetSerialize(FArchive& Ar) { Ar << Time; }

	operator FSimTime() const { return Time; }

	using T = FNetworkSimTime;
	T& operator+= (const T &rhs) { this->Time += rhs.Time; return(*this); }
	T& operator-= (const T &rhs) { this->Time -= rhs.Time; return(*this); }
	
	T operator+ (const T &rhs) const { return T(this->Time + rhs.Time); }
	T operator- (const T &rhs) const { return T(this->Time - rhs.Time); }

	bool operator<  (const T &rhs) const { return(this->Time < rhs.Time); }
	bool operator<= (const T &rhs) const { return(this->Time <= rhs.Time); }
	bool operator>  (const T &rhs) const { return(this->Time > rhs.Time); }
	bool operator>= (const T &rhs) const { return(this->Time >= rhs.Time); }
	bool operator== (const T &rhs) const { return(this->Time == rhs.Time); }
	bool operator!= (const T &rhs) const { return(this->Time != rhs.Time); }

private:

	FSimTime Time = 0;

	constexpr FNetworkSimTime(const FSimTime& InTime) { Time = InTime; }
};
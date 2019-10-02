// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkSimulationModelBuffer.h"

// ---------------------------------------------------------------------------------------------------------------------
//	BufferTypes: template helpers for addressing the different buffer types of the system.
// ---------------------------------------------------------------------------------------------------------------------

// Enum to refer to buffer type. These are used as template arguments to write generic code that can act on any of the buffers.
enum ENetworkSimBufferTypeId
{
	Input,
	Sync,
	Aux,
	Debug
};

inline FString LexToString(ENetworkSimBufferTypeId A)
{
	switch(A)
	{
		case Input: return TEXT("Input");
		case Sync: return TEXT("Sync");
		case Aux: return TEXT("Aux");
		case Debug: return TEXT("Debug");
	};
	return TEXT("Unknown");
}

// Helper needed to specialize TNetworkSimBufferTypes::Get (must be done outside of templated struct)
template<typename TBufferTypes, ENetworkSimBufferTypeId BufferId> 
struct TSelectTypeHelper
{
	using type = void;
};

template<typename TBufferTypes>
struct TSelectTypeHelper<TBufferTypes, Input>
{
	using type = typename TBufferTypes::TInputCmd;
};

template<typename TBufferTypes>
struct TSelectTypeHelper<TBufferTypes, Sync>
{
	using type = typename TBufferTypes::TSyncState;
};

template<typename TBufferTypes>
struct TSelectTypeHelper<TBufferTypes, Aux>
{
	using type = typename TBufferTypes::TAuxState;
};

template<typename TBufferTypes>
struct TSelectTypeHelper<TBufferTypes, Debug>
{
	using type = typename TBufferTypes::TDebugState;
};

// A collection of the system's buffer types. This allows us to collapse the 4 types into a single type to use a template argument elsewhere.
template<typename InInputCmd, typename InSyncState, typename InAuxState, typename InDebugState = FNetSimProcessedFrameDebugInfo>
struct TNetworkSimBufferTypes
{
	// Quick access to types when you know what you want
	using TInputCmd = InInputCmd;
	using TSyncState = InSyncState;
	using TAuxState = InAuxState;
	using TDebugState = InDebugState;

	// Template access via ENetworkSimBufferTypeId when "which buffer" is parameterized
	template<ENetworkSimBufferTypeId Id>
	struct select_type
	{
		using type = typename TSelectTypeHelper< TNetworkSimBufferTypes<TInputCmd, TSyncState, TAuxState, TDebugState>, Id >::type;
	};
};

// ---------------------------------------------------------------------------------------------------------------------
//	TNetworkSimBufferContainer
//	Container for the actual replicated buffers that the system uses.
//	Has compile time accessors for retrieving the buffers based on templated enum value.
// ---------------------------------------------------------------------------------------------------------------------

// Helper struct for enum-based access. This has to be done in an outside struct because we cant specialize inside TNetworkSimBufferContainer on all compilers
template<typename TContainer, ENetworkSimBufferTypeId BufferId>
struct TBufferGetterHelper
{
	static typename TContainer::template select_buffer_type<BufferId>::type& Get(TContainer& Container)
	{
		static_assert(!BufferId, "Failed to find specialized Get for your BufferId");
	}
};

template<typename TContainer>
struct TBufferGetterHelper<TContainer, ENetworkSimBufferTypeId::Input>
{
	static typename TContainer::TInputBuffer& Get(TContainer& Container)
	{
		return Container.Input;
	}
};
template<typename TContainer>
struct TBufferGetterHelper<TContainer, ENetworkSimBufferTypeId::Sync>
{
	static typename TContainer::TSyncBuffer& Get(TContainer& Container)
	{
		return Container.Sync;
	}
};
template<typename TContainer>
struct TBufferGetterHelper<TContainer, ENetworkSimBufferTypeId::Aux>
{
	static typename TContainer::TAuxBuffer& Get(TContainer& Container)
	{
		return Container.Aux;
	}
};
template<typename TContainer>
struct TBufferGetterHelper<TContainer, ENetworkSimBufferTypeId::Debug>
{
	static typename TContainer::TDebugBuffer& Get(TContainer& Container)
	{
		return Container.Debug;
	}
};

template<typename T>
struct TNetworkSimBufferContainer
{
	// Collection of types we were assigned
	using TBufferTypes = T;

	// helper that returns the buffer type for a given BufferId (not the underlying type: the actual TReplicatedBuffer<TUnderlyingType>
	template<ENetworkSimBufferTypeId TypeId>
	struct select_buffer_type
	{
		// We are just wrapping a TReplicationBuffer around whatever type TBufferTypes::select_type returns (which returns the underlying type)
		using type = TReplicationBuffer<typename TBufferTypes::template select_type<TypeId>::type>;
	};

	// The buffer types. This may look intimidating but is not that bad!
	// We are using select_buffer_type to decide what the type should be for a given ENetworkSimBufferTypeId.
	using TInputBuffer = typename select_buffer_type<ENetworkSimBufferTypeId::Input>::type;
	using TSyncBuffer = typename select_buffer_type<ENetworkSimBufferTypeId::Sync>::type;
	using TAuxBuffer = typename select_buffer_type<ENetworkSimBufferTypeId::Aux>::type;
	using TDebugBuffer = typename select_buffer_type<ENetworkSimBufferTypeId::Debug>::type;

	// The buffers themselves. Types are already declared above.
	// If you are reading just to find the damn underlying type here, its TReplicationBuffer< whatever your struct type is >
	TInputBuffer Input;
	TSyncBuffer Sync;
	TAuxBuffer Aux;
	TDebugBuffer Debug;

	// Finally, template accessor for getting buffers based on enum. This is really what all this junk is about.
	// This allows other templated classes in the system to access a specific buffer from another templated argument
	template<ENetworkSimBufferTypeId BufferId>
	typename select_buffer_type<BufferId>::type& Get()
	{
		return TBufferGetterHelper<TNetworkSimBufferContainer<T>, BufferId>::Get(*this);
	}
};


// ----------------------------------------------------------------------------------------------------------------------------------------------
//		Tick and time keeping related structures
// ----------------------------------------------------------------------------------------------------------------------------------------------

// The main Simulation time type. All sims use this to talk about time.
struct FNetworkSimTime
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
	static inline FNetworkSimTime FromMSec(const FSimTime& InTime) { return FNetworkSimTime(InTime); } 
	static inline FNetworkSimTime FromRealTimeSeconds(const FRealTime& InRealTime) { return FNetworkSimTime( static_cast<FSimTime>(InRealTime * GetRealToSimFactor())); } 
	FRealTime ToRealTimeSeconds() const { return (Time * GetSimToRealFactor()); }

	// Direct casts to "real time MS" which should be rarely used in practice (TRealTimeAccumulator only current case). All other cases of "real time" imply seconds.
	static inline FNetworkSimTime FromRealTimeMS(const FRealTime& InRealTime) { return FNetworkSimTime( static_cast<FSimTime>(InRealTime)); } 
	FRealTime ToRealTimeMS() const { return static_cast<FRealTime>(Time); }

	FString ToString() const { return LexToString(this->Time); }

	bool IsPositive() const { return (Time > 0); }
	bool IsNegative() const { return (Time < 0); }
	void Reset() { Time = 0; }

	// FIXME
	void NetSerialize(FArchive& Ar) { Ar << Time; }

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

	FNetworkSimTime(const FSimTime& InTime) { Time = InTime; }
};

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
//
// ----------------------------------------------------------------------------------------------------------------------------------------------

// Holds active state for simulation ticking: what inputs we have processed, how time we have simulated, how much we are allowed to, etc.
template<typename TickSettings=TNetworkSimTickSettings<>>
struct TSimulationTickState
{
	using TSettings = TickSettings;

	int32 LastProcessedInputKeyframe = 0;	// The last input keyframe that we processed
	int32 MaxAllowedInputKeyframe = 0;		// The max input keyframe that we are allowed to process (e.g, don't process input past this keyframe yet)
	
	FNetworkSimTime GetTotalProcessedSimulationTime() const 
	{ 
		return TotalProcessedSimulationTime; 
	}

	void SetTotalProcessedSimulationTime(const FNetworkSimTime& SimTime, int32 Keyframe)
	{
		TotalProcessedSimulationTime = SimTime;
		SimulationTimeBuffer.ResetNextHeadKeyframe(Keyframe);
		*SimulationTimeBuffer.GetWriteNext() = SimTime;
	}

	void IncrementTotalProcessedSimulationTime(const FNetworkSimTime& DeltaSimTime, int32 Keyframe)
	{
		TotalProcessedSimulationTime += DeltaSimTime;
		*SimulationTimeBuffer.GetWriteNext() = TotalProcessedSimulationTime;
		ensure(SimulationTimeBuffer.GetHeadKeyframe() == Keyframe);
	}

	void InitSimulationTimeBuffer(int32 Size)
	{
		SimulationTimeBuffer.SetBufferSize(Size);
	}
		
	// Historic tracking of simulation time. This allows us to timestamp sync data as its produced
	TReplicationBuffer<FNetworkSimTime>	SimulationTimeBuffer;

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

	// How much granted simulation time is left to process
	FNetworkSimTime GetRemainingAllowedSimulationTime() const
	{
		return TotalAllowedSimulationTime - TotalProcessedSimulationTime;
	}

	FNetworkSimTime GetTotalAllowedSimulationTime() const
	{
		return TotalAllowedSimulationTime;
	}

private:

	FNetworkSimTime TotalAllowedSimulationTime;	// Total time we have been "given" to process. We cannot process more simulation time than this: doing so would be speed hacking.
	FNetworkSimTime TotalProcessedSimulationTime;	// How much time we've actually processed. The only way to increment this is to process user commands or receive authoritative state from the network.

	TRealTimeAccumulator<TSettings>	RealtimeAccumulator;
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------------------------------------------------------------------------

// Wraps an input command (BaseType) in a NetworkSimulation time. 
template<typename BaseCmdType, typename TickSettings, bool IsFixedTick=(TickSettings::FixedStepMS!=0)>
struct TFrameCmd : public BaseCmdType
{
	FNetworkSimTime GetFrameDeltaTime() const { return FrameDeltaTime; }
	void SetFrameDeltaTime(const FNetworkSimTime& InTime) { FrameDeltaTime = InTime; }
	void NetSerialize(const FNetSerializeParams& P)
	{
		FrameDeltaTime.NetSerialize(P.Ar);
		BaseCmdType::NetSerialize(P); 
	}

private:
	FNetworkSimTime FrameDeltaTime;
};

// Fixed tick specialization
template<typename BaseCmdType, typename TickSettings>
struct TFrameCmd<BaseCmdType, TickSettings, true> : public BaseCmdType
{
	FNetworkSimTime GetFrameDeltaTime() const { return FNetworkSimTime::FromMSec(TickSettings::GetFixedStepMS()); }
	void SetFrameDeltaTime(const FNetworkSimTime& InTime) { }
	void NetSerialize(const FNetSerializeParams& P) { BaseCmdType::NetSerialize(P); }
};

// Helper to turn user supplied buffer types into the "real" buffer types: the InputCmd struct is wrapped in TFrameCmd
template<typename TUserBufferTypes, typename TTickSettings>
struct TInternalBufferTypes : TNetworkSimBufferTypes< 
	
	// InputCmds are wrapped in TFrameCmd, which will store an explicit sim delta time if we are not a fixed tick sim
	TFrameCmd< typename TUserBufferTypes::TInputCmd , TTickSettings>,

	typename TUserBufferTypes::TSyncState,	// SyncState Passes through
	typename TUserBufferTypes::TAuxState,	// Auxstate passes through
	typename TUserBufferTypes::TDebugState	// Debugstate passes through
>
{
};

/** Interface base for the simulation driver. This is what is used by the Network Sim Model internally. Basically, the non simulation specific functions that need to be defined. */
template<typename TBufferTypes>
class TNetworkSimDriverInterfaceBase
{
public:
	virtual FString GetDebugName() const = 0; // Used for debugging. Recommended to emit the simulation name and the actor name/role.
	virtual const UObject* GetVLogOwner() const = 0; // Owning object for Visual Logs

	virtual void InitSyncState(typename TBufferTypes::TSyncState& OutSyncState) const = 0;	// Called to create initial value of the sync state.
	virtual void ProduceInput(const FNetworkSimTime SimTime, typename TBufferTypes::TInputCmd&) = 0; // Called when the sim is ready to process new local input
	virtual void FinalizeFrame(const typename TBufferTypes::TSyncState& SyncState) = 0; // Called from the Network Sim at the end of the sim frame when there is new sync data.
};
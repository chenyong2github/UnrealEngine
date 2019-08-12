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
//
// ----------------------------------------------------------------------------------------------------------------------------------------------

// Holds all settings for a network sim related to ticking
template<uint32 InFixedStepMS=0, uint32 InMaxStepMS=0, typename TUnderlyingSimTimeType=uint32, typename TUnderlyingRealTimeType=float, int32 InRealToSimFactor=1000>
struct TNetworkSimTickSettings
{
	using TUnderlingSimTime = TUnderlyingSimTimeType;	// Underlying type of time. This type shouldn't be used directly (use TNetworkSimTime)
	using TRealTime = TUnderlyingRealTimeType;	// This is the "final" real time type. We do not wrap it in anything else.

	static_assert(!(InFixedStepMS!=0 && InMaxStepMS != 0), "MaxStepMS is only applicable when using variable step (FixedStepMS == 0)");

	enum 
	{
		MaxStepMS = InMaxStepMS,				// Max step. Only applicable to variable time step.
		FixedStepMS = InFixedStepMS,			// Fixed step. If 0, then we are "variable time step"
		RealToSimFactor = InRealToSimFactor		// Factor to go from RealTime (always seconds) to SimTime (MSec by default with factor of 1000)
	};

	// Typed accessors
	static constexpr TUnderlingSimTime GetMaxStepMS() { return static_cast<TUnderlingSimTime>(InMaxStepMS); }
	static constexpr TUnderlingSimTime GetFixedStepMS() { return static_cast<TUnderlingSimTime>(InFixedStepMS); }
	static constexpr TRealTime GetRealToSimFactor() { return static_cast<TRealTime>(InRealToSimFactor); }
};

// Actual time value. This by default will store time in MSec (ultimately determined by TickSettings::RealToSimFactor)
template<typename TickSettings>
struct TNetworkSimTime
{
	using TTime = typename TickSettings::TUnderlingSimTime;
	using TRealTime = typename TickSettings::TRealTime;

	TNetworkSimTime() { }
	TNetworkSimTime(const TTime& InTime) : Time(InTime) { }
	TNetworkSimTime(const TRealTime& InRealTime) : Time(InRealTime * TickSettings::RealToSimFactor) { }

	TRealTime ToRealTimeSeconds() const { return (Time / TickSettings::RealToSimFactor); }
	FString ToString() const { return LexToString(this->Time); }

	// FIXME
	void NetSerialize(FArchive& Ar) { Ar << Time; }

	using T = TNetworkSimTime;
	T& operator+= (const T &rhs) { this->Time += rhs.Time; return(*this); }
	T& operator-= (const T &rhs) { this->Time -= rhs.Time; return(*this); }
	
	T operator+ (const T &rhs) const { return T(this->Time - rhs.Time); }
	T operator- (const T &rhs) const { return T(this->Time - rhs.Time); }

	bool operator<  (const T &rhs) const { return(this->Time < rhs.Time); }
	bool operator<= (const T &rhs) const { return(this->Time <= rhs.Time); }
	bool operator>  (const T &rhs) const { return(this->Time > rhs.Time); }
	bool operator>= (const T &rhs) const { return(this->Time >= rhs.Time); }
	bool operator== (const T &rhs) const { return(this->Time == rhs.Time); }
	bool operator!= (const T &rhs) const { return(this->Time != rhs.Time); }

	TTime Time = 0;
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------------------------------------------------------------------------

// Helper for accumulating real time into sim time based on TickSettings
template<typename TickSettings, bool IsFixedTick=(TickSettings::FixedStepMS!=0)>
struct TRealTimeAccumulator
{
	using TRealTime = typename TickSettings::TRealTime;
	void Accumulate(TNetworkSimTime<TickSettings>& NetworkSimTime, TRealTime RealTimeSeconds)
	{
		// Non fixed step: just accumulate the time directly
		// Note that MaxStepMS enforcement does NOT belong here. Dropping time due to MaxStepMS would just make the sim run slower. MaxStepMS is used at the input processing level.
		NetworkSimTime += RealTimeSeconds;
	}
};

// Specialized version of FixedTicking. This accumulates real time that spills over into NetworkSimTime as it crosses the FixStep threshold
template<typename TickSettings>
struct TRealTimeAccumulator<TickSettings, true>
{
	using TRealTime = typename TickSettings::TRealTime;
	const TRealTime RealTimeFixedStep = TNetworkSimTime<TickSettings>(TickSettings::GetFixedStepMS() / TickSettings::GetRealToSimFactor()).ToRealTimeSeconds();

	void Accumulate(TNetworkSimTime<TickSettings>& NetworkSimTime, TRealTime RealTimeSeconds)
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

			NetworkSimTime += TNetworkSimTime<TickSettings>(NumFrames * TickSettings::GetFixedStepMS());
		}
	}
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

	int32 LastProcessedInputKeyframe;	// The last input keyframe that we processed
	int32 MaxAllowedInputKeyframe;		// The max input keyframe that we are allowed to process (e.g, don't process input past this keyframe yet)


	uint32 LastLocalInputGFrameNumber;	// Tracks the last time we accepted local input via GetNextInputForWrite. Used to guard against accidental input latency due to ordering of input/sim ticking.

	TNetworkSimTime<TSettings> TotalAllowedSimulationTime;	// Total time we have been "given" to process. We cannot process more simulation time than this: doing so would be speed hacking.
	TNetworkSimTime<TSettings> TotalProcessedSimulationTime;	// How much time we've actually processed. The only way to increment this is to process user commands or receive authoritative state from the network.
	
	template<typename TBufferTypes>
	typename TBufferTypes::TInputCmd* GetNextInputForWrite(TNetworkSimBufferContainer<TBufferTypes>& Buffers)
	{
		// Not really necessary anymore since input command is requested by the sim now instead of pushed into it
		ensure(LastLocalInputGFrameNumber != GFrameNumber);
		LastLocalInputGFrameNumber = GFrameNumber;

		typename TBufferTypes::TInputCmd* Next = nullptr;
		if (Buffers.Input.GetHeadKeyframe() == LastProcessedInputKeyframe)
		{
			// Only return a cmd if we have processed the last one. This is a bit heavy handed but is a good practice to start with. We want buffering of input to be very explicit, never something that accidentally happens.
			Next = Buffers.Input.GetWriteNext();
			*Next = typename TBufferTypes::TInputCmd();
		}
		return Next;
	}

	// "Grants" allowed simulation time to this tick state. That is, we are now allowed to advance the simulation by this amount the next time the sim ticks.
	// Note the input is RealTime in SECONDS. This is what the rest of the engine uses when dealing with float delta time.
	void GiveSimulationTime(float RealTimeSeconds)
	{
		RealtimeAccumulator.Accumulate(TotalAllowedSimulationTime, RealTimeSeconds);
	}

	TNetworkSimTime<TSettings> GetRemaningAllowedSimulationTime() const
	{
		return TotalAllowedSimulationTime - TotalProcessedSimulationTime;
	}

private:

	TRealTimeAccumulator<TSettings>	RealtimeAccumulator;
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------------------------------------------------------------------------

// Wraps an input command (BaseType) in a NetworkSimulation time. 
template<typename BaseCmdType, typename TickSettings, bool IsFixedTick=(TickSettings::FixedStepMS!=0)>
struct TFrameCmd : public BaseCmdType
{
	TNetworkSimTime<TickSettings> GetFrameDeltaTime() const { return FrameDeltaTime; }
	void SetFrameDeltaTime(const TNetworkSimTime<TickSettings>& InTime) { FrameDeltaTime = InTime; }

private:
	TNetworkSimTime<TickSettings> FrameDeltaTime;
};

// Fixed tick specialization
template<typename BaseCmdType, typename TickSettings>
struct TFrameCmd<BaseCmdType, TickSettings, true> : public BaseCmdType
{
	constexpr TNetworkSimTime<TickSettings> GetFrameDeltaTime() const { return TNetworkSimTime<TickSettings>(TickSettings::GetFixedStepMS()); }
	void SetFrameDeltaTime(const TNetworkSimTime<TickSettings>& InTime) { }
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
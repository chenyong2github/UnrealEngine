// Copyright 1998-2019 Epic Game s, Inc. All Rights Reserved.

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
	template<ENetworkSimBufferTypeId>
	struct select_type
	{
		using type = void;
	};

	template<>
	struct select_type<ENetworkSimBufferTypeId::Input>
	{
		using type = TInputCmd;
	};

	template<>
	struct select_type<ENetworkSimBufferTypeId::Sync>
	{
		using type = TSyncState;
	};

	template<>
	struct select_type<ENetworkSimBufferTypeId::Aux>
	{
		using type = TAuxState;
	};

	template<>
	struct select_type<ENetworkSimBufferTypeId::Debug>
	{
		using type = TDebugState;
	};
};

// ---------------------------------------------------------------------------------------------------------------------
//	Ticking and Input processing helpers. We want to support fixed tick systems (but variable is most important).
//	Whether your system is fixed or not depends on your input cmd type.
// ---------------------------------------------------------------------------------------------------------------------

// Basic struct trait for whether the input cmd is fixed or not. By default you are not fixed, you must specialize this template for your type and set value=true
template<typename T>
struct TNetworkSimInput_is_fixedtick
{
	static const bool value = false;
};

// Variable tick simulation time. For now this is a double.
template<bool IsFixedTick=false>
struct TSimulationTime
{
	TSimulationTime(double InTime=0.f) : Time(InTime) { }

	template<typename T>
	void AccumulateTimeFromInputCmd(const T& InputCmd)
	{
		Time += InputCmd.FrameDeltaTime;
	}
	
	void NetSerialize(const FNetSerializeParams& P)
	{
		// Temp: we can devise a system like keyframe replication to make this more efficient
		P.Ar << Time;
	}

	double Time = 0.f;
};

// Fixed tick simulation time. Just a frame counter.
template<>
struct TSimulationTime<true>
{
	TSimulationTime(int32 InTime=0) : Time(InTime) { }

	template<typename T>
	void AccumulateTimeFromInputCmd(const T& InputCmd)
	{
		Time++;
	}

	void NetSerialize(const FNetSerializeParams& P)
	{
		// Temp: we can devise a system like keyframe replication to make this more efficient
		P.Ar << Time;
	}

	int32 Time = 0;
};

// Actual struct to use to track time. Parameterized by your NetworkSimBufferTypes and uses the input cmd to determine if you are fixed or not.
template<typename TBufferTypes, typename TParent = TSimulationTime< TNetworkSimInput_is_fixedtick<typename TBufferTypes::TInputCmd>::value >>
struct TSimulationTimeKeeper : public TParent
{
	using T = TSimulationTimeKeeper<TBufferTypes>;
	using TParent::TParent;

	FString ToString() const { return LexToString(this->Time); }

	T& operator+= (const T &rhs) { this->Time += rhs.Time; return(*this); }
	T& operator-= (const T &rhs) { this->Time -= rhs.Time; return(*this); }
	
	T operator+ (const T &rhs) { return T(this->Time - rhs.Time); }
	T operator- (const T &rhs) { return T(this->Time - rhs.Time); }

	bool operator<  (const T &rhs) const { return(this->Time < rhs.Time); }
	bool operator<= (const T &rhs) const { return(this->Time <= rhs.Time); }
	bool operator>  (const T &rhs) const { return(this->Time > rhs.Time); }
	bool operator>= (const T &rhs) const { return(this->Time >= rhs.Time); }
	bool operator== (const T &rhs) const { return(this->Time == rhs.Time); }
	bool operator!= (const T &rhs) const { return(this->Time != rhs.Time); }
};


// ---------------------------------------------------------------------------------------------------------------------
//	TNetworkSimBufferContainer
//	Container for the actual replicated buffers that the system uses.
//	Has compile time accessors for retrieving the buffers based on templated enum value.
// ---------------------------------------------------------------------------------------------------------------------

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
		static_assert(!BufferId, "Failed to find specialized Get for your BufferId");
	}

	template<>
	TInputBuffer& Get<ENetworkSimBufferTypeId::Input>()
	{
		return Input;
	}

	template<>
	TSyncBuffer& Get<ENetworkSimBufferTypeId::Sync>()
	{
		return Sync;
	}

	template<>
	TAuxBuffer& Get<ENetworkSimBufferTypeId::Aux>()
	{
		return Aux;
	}

	template<>
	TDebugBuffer& Get<ENetworkSimBufferTypeId::Debug>()
	{
		return Debug;
	}
};


// ---------------------------------------------------------------------------------------------------------------------
//	TSimulationTicker
//	This holds the data for where we are in ticking the sim. Such as how much time we've ticked it, how much we are allowed to tick it.
//	It also holds input processing info: the last keyframe of the input buffer we processed and the last GFrameNumber we accepted input.
// ---------------------------------------------------------------------------------------------------------------------

template<typename TBufferTypes>
struct TSimulationTickInfo
{
	using FSimulationTime = TSimulationTimeKeeper<TBufferTypes>;

	int32 LastProcessedInputKeyframe;	// Tracks input keyframe that we last processed
	uint32 LastLocalInputGFrameNumber;	// Tracks the last time we accepted local input via GetNextInputForWrite. Used to guard against accidental input latency due to ordering of input/sim ticking.

	FSimulationTime MaxSimulationTime;			// Total max time we have been "given" to process. We cannot process more simulation time than this: doing so would be speed hacking.
	FSimulationTime ProcessedSimulationTime;	// How much time we've actually processed. The only way to increment this is to process user commands or receive authoritative state from the network.

	using TInputCmd = typename TBufferTypes::TInputCmd;
	TInputCmd* GetNextInputForWrite(TNetworkSimBufferContainer<TBufferTypes>& Buffers)
	{
		ensure(LastLocalInputGFrameNumber != GFrameNumber);
		LastLocalInputGFrameNumber = GFrameNumber;

		TInputCmd* Next = nullptr;
		if (Buffers.Input.GetHeadKeyframe() == LastProcessedInputKeyframe)
		{
			// Only return a cmd if we have processed the last one. This is a bit heavy handed but is a good practice to start with. We want buffering of input to be very explicit, never something that accidentally happens.
			Next = Buffers.Input.GetWriteNext();
			*Next = TInputCmd();
		}
		return Next;
	}
};
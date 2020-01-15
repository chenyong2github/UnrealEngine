// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "NetworkedSimulationModelTick.h"

// ---------------------------------------------------------------------------------------------------------------------
//	BufferTypes: template helpers for addressing the different buffer types of the system.
// ---------------------------------------------------------------------------------------------------------------------

// Enum to refer to buffer type. These are used as template arguments to write generic code that can act on any of the buffers.
enum class ENetworkSimBufferTypeId : uint8
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
		case ENetworkSimBufferTypeId::Input: return TEXT("Input");
		case ENetworkSimBufferTypeId::Sync: return TEXT("Sync");
		case ENetworkSimBufferTypeId::Aux: return TEXT("Aux");
		case ENetworkSimBufferTypeId::Debug: return TEXT("Debug");
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
struct TSelectTypeHelper<TBufferTypes, ENetworkSimBufferTypeId::Input>
{
	using type = typename TBufferTypes::TInputCmd;
};

template<typename TBufferTypes>
struct TSelectTypeHelper<TBufferTypes, ENetworkSimBufferTypeId::Sync>
{
	using type = typename TBufferTypes::TSyncState;
};

template<typename TBufferTypes>
struct TSelectTypeHelper<TBufferTypes, ENetworkSimBufferTypeId::Aux>
{
	using type = typename TBufferTypes::TAuxState;
};

template<typename TBufferTypes>
struct TSelectTypeHelper<TBufferTypes, ENetworkSimBufferTypeId::Debug>
{
	using type = typename TBufferTypes::TDebugState;
};

template<typename T>
struct HasNetSerialize
{
	template<typename U, void (U::*)(const FNetSerializeParams& P)> struct SFINAE {};
	template<typename U> static char Test(SFINAE<U, &U::NetSerialize>*);
	template<typename U> static int Test(...);
	static const bool Has = sizeof(Test<T>(0)) == sizeof(char);
};

template<typename T>
struct HasLog
{
	template<typename U, void (U::*)(FStandardLoggingParameters& P) const> struct SFINAE {};
	template<typename U> static char Test(SFINAE<U, &U::Log>*);
	template<typename U> static int Test(...);
	static const bool Has = sizeof(Test<T>(0)) == sizeof(char);
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

	// Must implement NetSerialize and Log functions. This purposefully does not pass on inherited methods
	static_assert(HasNetSerialize<InInputCmd>::Has == true, "InputCmd Must implement NetSerialize");
	static_assert(HasNetSerialize<InSyncState>::Has == true, "SyncState Must implement NetSerialize");
	static_assert(HasNetSerialize<InAuxState>::Has == true, "AuxState Must implement NetSerialize");
	static_assert(HasNetSerialize<InDebugState>::Has == true, "DebugState Must implement NetSerialize");

	static_assert(HasLog<InInputCmd>::Has == true, "InputCmd Must implement Log");
	static_assert(HasLog<InSyncState>::Has == true, "SyncState Must implement Log");
	static_assert(HasLog<InAuxState>::Has == true, "AuxState Must implement Log");
	static_assert(HasLog<InDebugState>::Has == true, "DebugState Must implement Log");
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	FrameCmd - in variable tick simulations we store the timestep of each frame with the input. TFrameCmd wraps the user struct to do this.
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

	void Log(FStandardLoggingParameters& P) const { BaseCmdType::Log(P); }

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
	void Log(FStandardLoggingParameters& P) const { BaseCmdType::Log(P); }
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
// "Internal" Buffers: Helper to turn user supplied buffer types into the "real" buffer types: the InputCmd struct is wrapped in TFrameCmd
//		-Outside/User code will always use the user supplied buffer types in callbacks/APIs etc
//		-Internal code to the TNetworkedSimulationModel will use the internal buffer types
// ----------------------------------------------------------------------------------------------------------------------------------------------
template<typename TUserBufferTypes, typename TTickSettings>
struct TInternalBufferTypes : TNetworkSimBufferTypes< 
	
	// InputCmds are wrapped in TFrameCmd, which will store an explicit sim delta time if we are not a fixed tick sim
	TFrameCmd< typename TUserBufferTypes::TInputCmd , TTickSettings>,

	typename TUserBufferTypes::TSyncState,	// SyncState passes through
	typename TUserBufferTypes::TAuxState,	// Auxstate passes through
	typename TUserBufferTypes::TDebugState	// Debugstate passes through
>
{
};
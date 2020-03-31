// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "NetworkedSimulationModelTick.h"
#include "Misc/EnumClassFlags.h"

// ---------------------------------------------------------------------------------------------------------------------
//	BufferTypes: template helpers for addressing the different buffer types of the system.
// ---------------------------------------------------------------------------------------------------------------------

// Enum to refer to buffer type. These are used as template arguments to write generic code that can act on any of the buffers.
enum class ENetworkSimBufferTypeId : uint8
{
	Input,
	Sync,
	Aux
};

inline FString LexToString(ENetworkSimBufferTypeId A)
{
	switch(A)
	{
		case ENetworkSimBufferTypeId::Input: return TEXT("Input");
		case ENetworkSimBufferTypeId::Sync: return TEXT("Sync");
		case ENetworkSimBufferTypeId::Aux: return TEXT("Aux");
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
template<typename InInputCmd, typename InSyncState, typename InAuxState>
struct TNetworkSimBufferTypes
{
	// Quick access to types when you know what you want
	using TInputCmd = InInputCmd;
	using TSyncState = InSyncState;
	using TAuxState = InAuxState;

	// Template access via ENetworkSimBufferTypeId when "which buffer" is parameterized
	template<ENetworkSimBufferTypeId Id>
	struct select_type
	{
		using type = typename TSelectTypeHelper< TNetworkSimBufferTypes<TInputCmd, TSyncState, TAuxState>, Id >::type;
	};

	// Must implement NetSerialize and Log functions. This purposefully does not pass on inherited methods
	static_assert(HasNetSerialize<InInputCmd>::Has == true, "InputCmd Must implement NetSerialize");
	static_assert(HasNetSerialize<InSyncState>::Has == true, "SyncState Must implement NetSerialize");
	static_assert(HasNetSerialize<InAuxState>::Has == true, "AuxState Must implement NetSerialize");

	static_assert(HasLog<InInputCmd>::Has == true, "InputCmd Must implement Log");
	static_assert(HasLog<InSyncState>::Has == true, "SyncState Must implement Log");
	static_assert(HasLog<InAuxState>::Has == true, "AuxState Must implement Log");
};

// ------------------------------------------------

// Wraps FrameDelta time. In fixed tick mode, this is specialized to be empty and return the known-at-compile-time step
template<typename TickSettings, bool IsFixedTick=(TickSettings::FixedStepMS!=0)>
struct TFrameDeltaTimeWrapper
{
	FNetworkSimTime Get() const { return FrameDeltaTime; }
	void Set(const FNetworkSimTime& InTime) { FrameDeltaTime = InTime; }
	void NetSerialize(const FNetSerializeParams& P)	{ FrameDeltaTime.NetSerialize(P.Ar); }

private:
	FNetworkSimTime FrameDeltaTime;
};

template<typename TickSettings>
struct TFrameDeltaTimeWrapper<TickSettings, true>
{
	FNetworkSimTime Get() const { return FNetworkSimTime::FromMSec(TickSettings::GetFixedStepMS()); }
	void Set(const FNetworkSimTime& InTime) { }
	void NetSerialize(const FNetSerializeParams& P) { }
};

// The actual per-frame data the system keeps track of
//template<typename TUserBufferTypes, typename TTickSettings>

enum class ESimulationFrameStateFlags : uint8
{
	InputWritten	= 1 << 0,	// Input has been written
};

ENUM_CLASS_FLAGS(ESimulationFrameStateFlags)

// This is what we actually store in contiguous memory, per frame.
template<typename Model>
struct TSimulationFrameStateBase
{
	using BufferTypes = typename Model::BufferTypes;
	using TickSettings = typename Model::TickSettings;

	using TInputCmd	= typename BufferTypes::TInputCmd;
	using TSyncState = typename BufferTypes::TSyncState;

	uint8 Flags = 0;
	FNetworkSimTime	TotalSimTime;
	TFrameDeltaTimeWrapper<TickSettings> FrameDeltaTime;

	TInputCmd	InputCmd;
	TSyncState	SyncState;

	bool HasFlag(const ESimulationFrameStateFlags& F) const { return (Flags & (uint8)F) > 0; }
	void SetFlag(const ESimulationFrameStateFlags& F) { Flags |= (uint8)F; }
	void ClearFlags() { Flags = 0; };
	void ResetFlagsTo(const ESimulationFrameStateFlags& F) { Flags = (uint8)F; }
};

// Outward facing struct. This allows user simulations to extend the internal FrameState struct if needed.
// Use case would be custom RepController logic that needed to do additional per-frame state tracking.
template<typename Model>
struct TSimulationFrameState : TSimulationFrameStateBase<Model> { };
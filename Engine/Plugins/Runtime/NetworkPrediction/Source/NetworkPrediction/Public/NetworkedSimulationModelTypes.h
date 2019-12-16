// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkedSimulationModelBuffer.h"
#include "NetworkedSimulationModelCues.h"
#include "NetworkedSimulationModelTime.h"

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
		static_assert(!Container, "Failed to find specialized Get for your BufferId");
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

// Helper for accessing a netsim buffer given the underlying type
namespace NetSimBufferSelect
{
	template<typename TContainer, typename TState>
	typename TEnableIf< TIsDerivedFrom<typename TContainer::TInputCmd, TState>::IsDerived, typename TContainer::TInputBuffer&>::Type
	Get(TContainer& Container)
	{
		return Container.Input;
	}

	template<typename TContainer, typename TState>
	typename TEnableIf< TIsDerivedFrom<typename TContainer::TSyncState, TState>::IsDerived, typename TContainer::TSyncBuffer&>::Type
	Get(TContainer& Container)
	{
		return Container.Sync;
	}

	template<typename TContainer, typename TState>
	typename TEnableIf< TIsDerivedFrom<typename TContainer::TAuxState, TState>::IsDerived, typename TContainer::TAuxBuffer&>::Type
	Get(TContainer& Container)
	{
		return Container.Aux;
	}

	template<typename TContainer, typename TState>
	typename TEnableIf< TIsDerivedFrom<typename TContainer::TDebugState, TState>::IsDerived, typename TContainer::TDebugState&>::Type
	Get(TContainer& Container)
	{
		return Container.Debug;
	}
}

// Struct that encapsulates writing a new element to a buffer. This is used to allow a new Aux state to be created in the ::SimulationTick loop.
template<typename T>
struct TLazyStateAccessor
{
	TLazyStateAccessor(TUniqueFunction<T*()> && InFunc)
		: GetWriteNextFunc(MoveTemp(InFunc)) { }

	T* GetWriteNext() const
	{
		if (CachedWriteNext == nullptr)
		{
			CachedWriteNext = GetWriteNextFunc();
		}
		return CachedWriteNext;
	}

private:

	mutable T* CachedWriteNext = nullptr;
	TUniqueFunction<T*()> GetWriteNextFunc;
};

// The main container for all of our buffers
template<typename InBufferTypes>
struct TNetworkSimBufferContainer
{
	// Collection of types we were assigned
	using TBufferTypes = InBufferTypes;

	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;
	using TDebugState = typename TBufferTypes::TDebugState;

	// helper that returns the buffer type for a given BufferId (not the underlying type: the actual TReplicatedBuffer<TUnderlyingType>
	template<ENetworkSimBufferTypeId TypeId>
	struct select_buffer_type
	{
		// We are just wrapping a TNetworkSimContiguousBuffer around whatever type TBufferTypes::select_type returns (which returns the underlying type)
		using type = TNetworkSimContiguousBuffer<typename TBufferTypes::template select_type<TypeId>::type>;
	};

	template<ENetworkSimBufferTypeId TypeId>
	struct select_buffer_type_sparse
	{
		// We are just wrapping a TNetworkSimSparseBuffer around whatever type TBufferTypes::select_type returns (which returns the underlying type)
		using type = TNetworkSimSparseBuffer<typename TBufferTypes::template select_type<TypeId>::type>;
	};

	// The buffer types. This may look intimidating but is not that bad!
	// We are using select_buffer_type to decide what the type should be for a given ENetworkSimBufferTypeId.
	using TInputBuffer = typename select_buffer_type<ENetworkSimBufferTypeId::Input>::type;
	using TSyncBuffer = typename select_buffer_type<ENetworkSimBufferTypeId::Sync>::type;
	using TAuxBuffer = typename select_buffer_type_sparse<ENetworkSimBufferTypeId::Aux>::type;
	using TDebugBuffer = typename select_buffer_type<ENetworkSimBufferTypeId::Debug>::type;

	// The buffers themselves. Types are already declared above.
	// If you are reading just to find the damn underlying type here, its (TNetworkSimSparseBuffer||TNetworkSimContiguousBuffer) < whatever your struct type is >
	TInputBuffer Input;
	TSyncBuffer Sync;
	TAuxBuffer Aux;
	TDebugBuffer Debug;

	FCueDispatcher CueDispatcher;

	// Finally, template accessor for getting buffers based on enum. This is really what all this junk is about.
	// This allows other templated classes in the system to access a specific buffer from another templated argument
	template<ENetworkSimBufferTypeId BufferId>
	typename select_buffer_type<BufferId>::type& Get()
	{
		return TBufferGetterHelper<TNetworkSimBufferContainer<TBufferTypes>, BufferId>::Get(*this);
	}
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	Accessors - helper structs that provide safe/cleaner access to the underlying NetSim states/events
//	This is the equivlent of directly calling TNetworkedSimulationModel::GetPendingStateRead() / GetPendingStateWrite()
//
//	This is useful because it is not always practical to have a typed pointer to your TNetworkedSimulationModel instance. For example,
//	the TNetworkedSimulationModel may be templated and conditionally instantiated (for example fix tick vs non fixed tick models, and eventually
//	we may offer more memory allocation related templated parameters).
//		
//	Essentially, this allows you to plop a TNetSimStateAccessor<MyStateType> on a class and have it bind to any TNetworkedSimulationModel instantiation  
//	that has uses that type.
//
//	See additional comments in TNetworkedSimulationModel::GetPendingStateRead() / GetPendingStateWrite()
// ----------------------------------------------------------------------------------------------------------------------------------------------


template<typename TState>
struct TNetSimStateAccessor
{
	// Bind to the NetsimModel that we are accessing. This is a templated method so that a single declared TNetSimStateAccessor can bind to any instantiated netsim model
	// that has the underlying type of the accessor. This allows, for example, templated netsim models to be instantiated and bind to a single accessor. (E.g, variable or fixed tick sim)
	template<typename TNetworkSimModel>
	void Bind(TNetworkSimModel* NetSimModel)
	{
		GetStateFunc = [NetSimModel](bool bWrite, TState*& OutState, bool& OutSafe)
		{
			auto& Buffer = NetSimBufferSelect::Get<TNetworkSimBufferContainer<typename TNetworkSimModel::TBufferTypes>, TState>(NetSimModel->Buffers);
			OutState = bWrite ? Buffer.WriteFrameInitializedFromHead(NetSimModel->Ticker.PendingFrame) : Buffer[NetSimModel->Ticker.PendingFrame];
			OutSafe = NetSimModel->Ticker.bUpdateInProgress;
		};
	}

	void Clear()
	{
		GetStateFunc = nullptr;
	}

	/** Gets the current (PendingFrame) state for reading. This is not expected to fail outside of startup/shutdown edge cases */
	const TState* GetStateRead() const
	{
		TState* State = nullptr;
		bool bSafe = false;
		if (GetStateFunc)
		{
			GetStateFunc(false, State, bSafe);
		}
		return State;
	}

	/** Gets the current (PendingFrame) state for writing. This is expected to fail outside of the core update loop when bHasAuthority=false. (E.g, it is not safe to predict writes) */
	TState* GetStateWrite(bool bHasAuthority) const
	{
		TState* State = nullptr;
		bool bSafe = false;
		if (GetStateFunc)
		{
			GetStateFunc(true, State, bSafe);
		}
		return (bHasAuthority || bSafe) ? State : nullptr;
	}

private:

	TFunction<void(bool bForWrite, TState*& OutState, bool& OutSafe)> GetStateFunc;
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

// Helper to turn user supplied buffer types into the "real" buffer types: the InputCmd struct is wrapped in TFrameCmd
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

/** This is the "system driver", it has functions that the TNetworkedSimulationModel needs to call internally, that are specific to the types but not specific to the simulation itself. */
template<typename TBufferTypes>
class TNetworkedSimulationModelDriver
{
public:
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState= typename TBufferTypes::TAuxState;

	// Debug string that can be used in internal warning/error logs
	virtual FString GetDebugName() const = 0;

	// Owning object for Visual Logs so that the system can emit them internally
	virtual const AActor* GetVLogOwner() const = 0;

	// Call to visual log the given states. Note that not all 3 will always be present and you should check for nullptrs.
	virtual void VisualLog(const TInputCmd* Input, const TSyncState* Sync, const TAuxState* Aux, const FVisualLoggingParameters& SystemParameters) const = 0;
	
	// Called whenever the sim is ready to process new local input.
	virtual void ProduceInput(const FNetworkSimTime SimTime, TInputCmd&) = 0;
	
	// Called from the Network Sim at the end of the sim frame when there is new sync data.
	virtual void FinalizeFrame(const typename TBufferTypes::TSyncState& SyncState, const typename TBufferTypes::TAuxState& AuxState) = 0;
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	Simulation Input/Output parameters. These are the data structures passed into the simulation code each frame
// ----------------------------------------------------------------------------------------------------------------------------------------------

// Input state: const references to the InputCmd/SyncState/AuxStates
template<typename TBufferTypes>
struct TNetSimInput
{
	const typename TBufferTypes::TInputCmd& Cmd;
	const typename TBufferTypes::TSyncState& Sync;
	const typename TBufferTypes::TAuxState& Aux;

	TNetSimInput(const typename TBufferTypes::TInputCmd& InInputCmd, const typename TBufferTypes::TSyncState& InSync, const typename TBufferTypes::TAuxState& InAux)
		: Cmd(InInputCmd), Sync(InSync), Aux(InAux) { }	
	
	// Allows implicit downcasting to a parent simulation class's input types
	template<typename T>
	TNetSimInput(const TNetSimInput<T>& Other)
		: Cmd(Other.Cmd), Sync(Other.Sync), Aux(Other.Aux) { }
};

// Output state: the output SyncState (always created) and TNetSimLazyWriter for the AuxState (created on demand since every tick does not generate a new aux frame)
template<typename TBufferTypes>
struct TNetSimOutput
{
	typename TBufferTypes::TSyncState& Sync;
	const TNetSimLazyWriter<typename TBufferTypes::TAuxState>& Aux;
	FCueDispatcher& CueDispatch;

	TNetSimOutput(typename TBufferTypes::TSyncState& InSync, const TNetSimLazyWriter<typename TBufferTypes::TAuxState>& InAux, FCueDispatcher& InCueDispatch)
		: Sync(InSync), Aux(InAux), CueDispatch(InCueDispatch) { }
};


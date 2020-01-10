// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkedSimulationModelBuffer.h"
#include "NetworkedSimulationModelCues.h"
#include "NetworkedSimulationModelTime.h"
#include "NetworkedSimulationModelTraits.h"

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

// The main container for all of our buffers
template<typename Model>
struct TNetworkSimBufferContainer
{
	// Collection of types we were assigned
	using TBufferTypes = typename TNetSimModelTraits<Model>::InternalBufferTypes;

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

	TNetSimCueDispatcher<Model> CueDispatcher;

	// Finally, template accessor for getting buffers based on enum. This is really what all this junk is about.
	// This allows other templated classes in the system to access a specific buffer from another templated argument
	template<ENetworkSimBufferTypeId BufferId>
	typename select_buffer_type<BufferId>::type& Get()
	{
		return TBufferGetterHelper<TNetworkSimBufferContainer<Model>, BufferId>::Get(*this);
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
			auto& Buffer = NetSimBufferSelect::Get<TNetworkSimBufferContainer<typename TNetworkSimModel::Model>, TState>(NetSimModel->Buffers);
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
	FNetSimCueDispatcher& CueDispatch;

	TNetSimOutput(typename TBufferTypes::TSyncState& InSync, const TNetSimLazyWriter<typename TBufferTypes::TAuxState>& InAux, FNetSimCueDispatcher& InCueDispatch)
		: Sync(InSync), Aux(InAux), CueDispatch(InCueDispatch) { }
};

// Scoped helper to be used right before entering a call to the sim's ::SimulationTick function.
// Important to note that this advances the PendingFrame to the output Frame. So that any writes that occur to the buffers during this scope will go to the output frame.
template<typename Model>
struct TScopedSimulationTick
{
	TScopedSimulationTick(FSimulationTickState& InTicker, TNetSimCueDispatcher<Model>& InDispatcher, ESimulationTickContext TickContext, const int32& InOutputFrame, const FNetworkSimTime& InDeltaSimTime)
		: Ticker(InTicker), Dispatcher(InDispatcher), OutputFrame(InOutputFrame), DeltaSimTime(InDeltaSimTime)
	{
		check(Ticker.bUpdateInProgress == false);
		Ticker.PendingFrame = OutputFrame;
		Ticker.bUpdateInProgress = true;

		Dispatcher.PushContext({Ticker.GetTotalProcessedSimulationTime() + DeltaSimTime, TickContext}); // Cues "take place" at the end of the frame
	}
	~TScopedSimulationTick()
	{
		Ticker.IncrementTotalProcessedSimulationTime(DeltaSimTime, OutputFrame);
		Ticker.bUpdateInProgress = false;

		Dispatcher.PopContext();
	}
	FSimulationTickState& Ticker;
	TNetSimCueDispatcher<Model>& Dispatcher;
	const int32& OutputFrame;
	const FNetworkSimTime& DeltaSimTime;
};

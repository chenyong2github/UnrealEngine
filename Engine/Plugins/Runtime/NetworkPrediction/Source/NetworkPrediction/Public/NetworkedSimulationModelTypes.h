// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkedSimulationModelBuffer.h"
#include "NetworkedSimulationModelCues.h"
#include "NetworkedSimulationModelTime.h"
#include "NetworkedSimulationModelTraits.h"
#include "Trace/NetworkPredictionTrace.h"
#include "NetworkPredictionCheck.h"

// ---------------------------------------------------------------------------------------------------------------------
//	TNetworkedSimulationState:
//
//	This structure contains all the networked state used by the system.
//	
//	Simulation's state:
//		-FrameBuffer:	frame-to-frame state
//		-AuxBuffer:		sparse frame based state
//		-CueDispatcher: non simulation affecting event history, mappings
//		-TimingInfo:	time keeper for advancing simulation ticks
//	
//	-Frame Markers
//		-Pending		Next frame that will be ticked. Latest data.
//		-LatestInput	Latest frame to have input written to it.
//		-NextInput		Where next input should be written to.
//		-MaxTick		Max frame that is allowed to be ticked. To facilitate input buffering when necessary.
//		-Confirmed		Last valid frame. Frames prior to confirmed frame do not need to be kept around.
//
// ---------------------------------------------------------------------------------------------------------------------

template<typename Model>
struct TNetworkedSimulationState
{
	// Collection of types we were assigned
	using TBufferTypes = typename Model::BufferTypes;
	using TickSettings = typename Model::TickSettings;

	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;

	TNetSimCueDispatcher<Model> CueDispatcher;

	TNetworkedSimulationState()
		: FrameBuffer(64), AuxBuffer(64)
	{
	}

	int32 Capacity() const { return FrameBuffer.Capacity(); }
	FString DebugString() const { return FString::Printf(TEXT("PendingTick: %d MaxTick: %d LatestInput: %d NextIntput: %d Size: %d. PT: %s. IT: %s. MT: %s"), 
		PendingTickFrame, MaxTickFrame, LatestInputFrame, NextInputFrame, Capacity(), *GetTotalProcessedSimulationTime().ToString(), *GetTotalInputSimulationTime().ToString(), *GetTotalAllowedSimulationTime().ToString()); }
	
	// ---------------------------------------------------------------------
	// Frame State
	// ---------------------------------------------------------------------

	// Writes a new frame to the buffer, with the given explicit TotalSimTime.
	// This is used in places where we might break sim time continuity (NetRecvs) or are possibly resimulating over a frame and want to explicitly set the TotalSimTime.
	TSimulationFrameState<Model>* WriteFrameWithTime(int32 NewFrame, FNetworkSimTime TotalSimTime)
	{
		if (TSimulationFrameState<Model>* ExistingFrame = GetValidFrame(NewFrame))
		{
			ExistingFrame->TotalSimTime = TotalSimTime;
			return ExistingFrame;
		}
		
		const int32 Delta = NewFrame - PendingTickFrame;
		if (Delta >= Capacity())
		{
			// PendingTickFrame must always be preserved
			UE_NP_TRACE_SYSTEM_FAULT("WriteFrameWithTime frame %d outside of buffer range. Reseting contents. %s", PendingTickFrame, *DebugString());
				
			TSimulationFrameState<Model>& NewFrameState = FrameBuffer[NewFrame];
			NewFrameState = FrameBuffer[PendingTickFrame]; // Init with previous state
			NewFrameState.TotalSimTime = TotalSimTime;
			NewFrameState.ClearFlags();
				
			PendingTickFrame = NewFrame;
			MaxTickFrame = NewFrame-1;
			LatestInputFrame = INDEX_NONE;
			NextInputFrame = NewFrame;
			CheckInvariants();

			return &NewFrameState;
		}
			
		TSimulationFrameState<Model>& NewFrameState = FrameBuffer[NewFrame];
		NewFrameState.TotalSimTime = TotalSimTime;

		for (int32 Frame = LatestInputFrame+1; Frame <= NewFrame; ++Frame)
		{
			FrameBuffer[Frame].ClearFlags();
		}
		
		return &NewFrameState;
	}

	// Writes a sequential frame to the buffer, even if it creates a gap. This is used in places where we want to maintain continuity of TotalSimTime for the new frame.
	// That is, regardless if frame# is not continuous, we want this new frame to have the correct TotalSimTime, based on the previous valid frame.
	//
	// If the frame already exists, will just return the frame
	// If this causes a new frame to be created, its flags will be cleared and TotalSimTime will be set for you. But contents will otherwise be stale
	// If a gap is created, those frames will also have flags cleared
	TSimulationFrameState<Model>* WriteFrameSequential(int32 NewFrame)
	{
		if (TSimulationFrameState<Model>* ExistingFrame = GetValidFrame(NewFrame))
		{
			CheckInvariants();
			return ExistingFrame;
		}
				
		const int32 Delta = NewFrame - PendingTickFrame;
		if (Delta >= Capacity())
		{
			// PendingTickFrame must always be preserved
			UE_NP_TRACE_SYSTEM_FAULT("WriteFrameSequential frame %d outside of buffer range. Reseting contents. %s", PendingTickFrame, *DebugString());

			TSimulationFrameState<Model>& NewFrameState = FrameBuffer[NewFrame];
			NewFrameState = FrameBuffer[PendingTickFrame]; // Init with previous state

			PendingTickFrame = NewFrame;
			MaxTickFrame = NewFrame-1;

			if (NewFrameState.HasFlag(ESimulationFrameStateFlags::InputWritten))
			{
				LatestInputFrame = NewFrame;
				NextInputFrame = NewFrame+1;
			}
			else
			{
				LatestInputFrame = INDEX_NONE;
				NextInputFrame = NewFrame;
			}
			
			CheckInvariants();
			return &NewFrameState;
		}

		// Create the new frame
		TSimulationFrameState<Model>& NewFrameState = FrameBuffer[NewFrame];
		NewFrameState.ClearFlags();

		// Clear gap frame flags so they don't get processed
		const int32 PrevFrame = FMath::Max(PendingTickFrame, LatestInputFrame);
		for (int32 Frame = PrevFrame+1; Frame < NewFrame; ++Frame)
		{
			FrameBuffer[Frame].ClearFlags();
		}

		// Look at the previous frame sequential frame and calculate the new Frame's TotalSimTime
		TSimulationFrameState<Model>& PrevFrameState = FrameBuffer[PrevFrame];
		
		if (PrevFrameState.HasFlag(ESimulationFrameStateFlags::InputWritten))
		{
			NewFrameState.TotalSimTime = PrevFrameState.FrameDeltaTime.Get() + PrevFrameState.TotalSimTime;
		}
		else
		{
			NewFrameState.TotalSimTime = PrevFrameState.TotalSimTime;
		}
		CheckInvariants();
		return &NewFrameState;
	}

	TSimulationFrameState<Model>* WriteFramePending()
	{
		return &FrameBuffer[PendingTickFrame];
	}

	TSimulationFrameState<Model>* ReadFrame(int32 Frame) { return GetValidFrame(Frame); }
	const TSimulationFrameState<Model>* ReadFrame(int32 Frame) const { return GetValidFrame(Frame); }

	// ---------------------------------------------------------------------
	// Aux State
	// ---------------------------------------------------------------------
	
	TAuxState* ReadAux(int32 Frame) { return AuxBuffer[Frame]; }
	const TAuxState* ReadAux(int32 Frame) const { return AuxBuffer[Frame]; }

	TNetSimLazyWriterFunc<TAuxState> GetAuxStateLazyWriter(int32 Frame) { return AuxBuffer.LazyWriter(Frame); }
	TAuxState* WriteAuxState(int32 Frame) { return AuxBuffer.WriteAtFrame(Frame); }

	// ---------------------------------------------------------------------
	// Ticking
	// ---------------------------------------------------------------------

	// Returns how much time has been ticked
	FNetworkSimTime GetTotalProcessedSimulationTime() const { return FrameBuffer[PendingTickFrame].TotalSimTime; }

	// Returns how much time worth of input has been submitted. E.g, if every submitted frame was processed, what would the TotalProcesedSimulationTime be?
	FNetworkSimTime GetTotalInputSimulationTime() const
	{ 
		if (LatestInputFrame != INDEX_NONE)
		{
			return FrameBuffer[LatestInputFrame].TotalSimTime + FrameBuffer[LatestInputFrame].FrameDeltaTime.Get();
		}
		return FrameBuffer[PendingTickFrame].TotalSimTime;
	}

	// How much time has been given to the simulation to be ticked
	FNetworkSimTime GetTotalAllowedSimulationTime() const {	return TotalAllowedSimulationTime; }

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
	
	// Scoped bool set while running a ::SimulationTick
	bool GetTickInProgress() const { return bTickInProgress; }	
	void SetTickInProgress(bool b) { bTickInProgress = b; }
		
	// The next frame that will be ticked: used as Input to create a new frame at PendingTickFrame+1.
	// This is where all OOB mods to state should happen. E.g, the "mutable" frame. Must be preserved whenever writing new frames.
	int32 GetPendingTickFrame() const { return PendingTickFrame; }
	void SetPendingTickFrame(int32 Frame) { PendingTickFrame = Frame; }

	// Oldest valid frame. This is set explicitly as authoritative state is received by clients. 
	// On server, it is the tail frame of the buffer: the oldest valid frame.
	int32 GetConfirmedFrame() const { return ConfirmedFrame == INDEX_NONE ? TailFrame() : ConfirmedFrame; }
	void SetConfirmedFrame(int32 Frame) { ConfirmedFrame = Frame; CheckInvariants(); }

	// Max frame we are allowed to process. This is how local buffer can be done.
	int32 GetMaxTickFrame() const { return MaxTickFrame; }
	void SetMaxTickFrame(int32 Frame) { MaxTickFrame = Frame; CheckInvariants(); }

	// The last frame that had input written to it. This can be ahead of PendingTickFrame but never more than 1 frame behind.
	int32 GetLatestInputFrame() const { return LatestInputFrame; }
	void SetLatestInputFrame(int32 Frame)
	{
		LatestInputFrame = Frame;
		NextInputFrame = Frame == INDEX_NONE ? PendingTickFrame : (Frame+1);
		CheckInvariants(); 
	}

	// Where input is expected to be written to next. Gaps in NetSerialization can cause this to not be the case though.
	int32 GetNextInputFrame() const { return NextInputFrame; }

	// Resets the system to the existing NewPendingTickFrame. This is used when we need to hard reset things to a given authoritative state
	void Reset(int32 NewPendingTickFrame)
	{
		PendingTickFrame = NewPendingTickFrame;
		ConfirmedFrame = NewPendingTickFrame;
		MaxTickFrame = INDEX_NONE;
		LatestInputFrame = INDEX_NONE;
		NextInputFrame = NewPendingTickFrame;

		TotalAllowedSimulationTime = FrameBuffer[PendingTickFrame].TotalSimTime;
		CheckInvariants();
	}

	void CheckInvariants()
	{
		npEnsureMsgf(IsValidFrame(PendingTickFrame), TEXT("PendingTickFrame not valid. %s"), *DebugString());
		npEnsureMsgf(ConfirmedFrame <= PendingTickFrame, TEXT("ConfirmedFrame %d <= PendingTickFrame"), ConfirmedFrame, PendingTickFrame);

		if (LatestInputFrame != INDEX_NONE)
		{
			npEnsureMsgf(MaxTickFrame <= LatestInputFrame, TEXT("MaxTickFrame %d <= LatestInputFrame %d"), MaxTickFrame, LatestInputFrame);
			npEnsureMsgf(PendingTickFrame <= LatestInputFrame+1, TEXT("PendingTickFrame %d <= (LatestInputFrame+1) %d"), PendingTickFrame, LatestInputFrame+1);
			npEnsureMsgf(LatestInputFrame == NextInputFrame-1, TEXT("LatestInputFrame %d != (NextInputFrame+1) %d"),  LatestInputFrame, NextInputFrame+1);			
			npEnsureMsgf(IsValidFrame(LatestInputFrame) && FrameBuffer[LatestInputFrame].HasFlag(ESimulationFrameStateFlags::InputWritten), TEXT("LatestInputFrame not valid. %s"), *DebugString());
		}

		if (MaxTickFrame != INDEX_NONE)
		{
			npEnsureMsgf(PendingTickFrame <= MaxTickFrame+1, TEXT("PendingTickFrame %d <= (MaxTickFrame+1) %d"), PendingTickFrame, MaxTickFrame+1);
		}

#if NP_CHECKS_AND_ENSURES_SLOW
		// If PendingTickFrame is ahead of input, ensure that calculated TotalSimTime is what we expected.
		// (If this fires, its likely LatestInputFrame is wrong. If we took a frame from a NetSerialize for example,
		// latest InputFrame should be reset.
		if (LatestInputFrame != INDEX_NONE)
		{
			if (LatestInputFrame < PendingTickFrame)
			{
				ensure(LatestInputFrame+1 == PendingTickFrame);
				FNetworkSimTime PendingFrameTotalSimTime = FrameBuffer[PendingTickFrame].TotalSimTime;
				FNetworkSimTime InputFramesExpectedTotalSimTime = FrameBuffer[LatestInputFrame].TotalSimTime + FrameBuffer[LatestInputFrame].FrameDeltaTime.Get();
				ensure(InputFramesExpectedTotalSimTime == PendingFrameTotalSimTime);
			}

			npEnsureMsgf(FrameBuffer[LatestInputFrame].HasFlag(ESimulationFrameStateFlags::InputWritten), TEXT("LatestInputFrame %d has no InputWritten"), LatestInputFrame);
		}
#endif

	}

private:

	int32 HeadFrame() const { return FMath::Max(PendingTickFrame, LatestInputFrame); }
	int32 TailFrame() const { return FMath::Max(ConfirmedFrame, HeadFrame() - FrameBuffer.Capacity() + 1); }
	bool IsValidFrame(int32 Frame) const { return Frame >= TailFrame() && Frame <= HeadFrame(); }

	const TSimulationFrameState<Model>* GetValidFrame(int32 Frame) const
	{
		return IsValidFrame(Frame) ? &FrameBuffer[Frame] : nullptr;
	}

	TSimulationFrameState<Model>* GetValidFrame(int32 Frame)
	{
		return IsValidFrame(Frame) ? &FrameBuffer[Frame] : nullptr;
	}

	bool bTickInProgress = false;

	int32 PendingTickFrame = 0;
	int32 NextInputFrame = 0;

	int32 MaxTickFrame = INDEX_NONE;
	int32 LatestInputFrame = INDEX_NONE;
	int32 ConfirmedFrame = INDEX_NONE;
	
	FNetworkSimTime TotalAllowedSimulationTime;
	TRealTimeAccumulator<TickSettings>	RealtimeAccumulator;
	
	TNetworkSimFrameBuffer<TSimulationFrameState<Model>> FrameBuffer;
	TNetworkSimAuxBuffer<TAuxState> AuxBuffer;
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
//	Explanation: During the scope of an ::SimulationTick call, we know exactly 'when' we are relative to what the server is processing. If the predicting client wants
//	to predict a change to sync/aux state during an update, the server will do it at the exact same time (assuming not a mis prediction). When a state change
//	happens "out of band" (outside an ::SimulationTick call) - we really have no way to correlate when the server will do it. While its tempting to think "we will get
//	a correction anyways, might as well guess at it and maybe get a smaller correction" - but this opens us up to other problems. The server may actually not 
//	change the state at all and you may not get an update that corrects you. You could add a timeout and track the state change somewhere but that really complicates
//	things and could leave you open to "double" problems: if the state change is additive, you may stack the authority change on top of the local predicted change, or
//	you may roll back the predicted change to then later receive the authority change.
// ----------------------------------------------------------------------------------------------------------------------------------------------

// Helper to specialize on Sync vs Aux state access
template<typename TNetworkSimModel, typename TState, bool IsSyncState=TIsDerivedFrom<typename TNetworkSimModel::TSyncState, TState>::Value>
struct TNetSimAccessorHelper
{
	// Sync State
	using TAccessFunc = TUniqueFunction<void(bool bForWrite, TState*& OutState, bool& OutSafe, const TCHAR* TraceStr)>;
	static TAccessFunc GetStateFunc(TNetworkSimModel* NetSimModel)
	{
		TAccessFunc Func = [NetSimModel](bool bForWrite, TState*& OutState, bool& OutSafe, const TCHAR* TraceStr)
		{
			if (bForWrite)
			{
				OutState = NetSimModel->WriteSyncState(TraceStr);
			}
			else
			{
				OutState = const_cast<TState*>((TState*)NetSimModel->ReadSyncState());
			}
			
			OutSafe = NetSimModel->State.GetTickInProgress();
		};
		return Func;
	}
};

template<typename TNetworkSimModel, typename TState>
struct TNetSimAccessorHelper<TNetworkSimModel, TState, false>
{
	// Aux State
	using TAccessFunc = TUniqueFunction<void(bool bForWrite, TState*& OutState, bool& OutSafe, const TCHAR* TraceStr)>;
	static TAccessFunc GetStateFunc(TNetworkSimModel* NetSimModel)
	{
		TAccessFunc Func = [NetSimModel](bool bForWrite, TState*& OutState, bool& OutSafe, const TCHAR* TraceStr)
		{
			if (bForWrite)
			{
				OutState = NetSimModel->WriteAuxState(TraceStr);
			}
			else
			{
				OutState = const_cast<TState*>((TState*)NetSimModel->ReadAuxState());
			}
			OutSafe = NetSimModel->State.GetTickInProgress();
		};
		return Func;
	}
};

template<typename TState>
struct TNetSimStateAccessor
{
	// Bind to the NetsimModel that we are accessing. This is a templated method so that a single declared TNetSimStateAccessor can bind to any instantiated netsim model
	// that has the underlying type of the accessor. This allows, for example, templated netsim models to be instantiated and bind to a single accessor. (E.g, variable or fixed tick sim)
	template<typename TNetworkSimModel>
	void Bind(TNetworkSimModel* NetSimModel)
	{
		GetStateFunc = TNetSimAccessorHelper<TNetworkSimModel, TState>::GetStateFunc(NetSimModel);
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
			GetStateFunc(false, State, bSafe, nullptr);
		}
		return State;
	}

	/** Gets the current (PendingFrame) state for writing. This is expected to fail outside of the core update loop when bHasAuthority=false. (E.g, it is not safe to predict writes) */
	TState* GetStateWrite(bool bHasAuthority, const TCHAR* TraceStr=nullptr) const
	{
		TState* State = nullptr;
		bool bSafe = false;
		if (GetStateFunc)
		{
			GetStateFunc(true, State, bSafe, TraceStr);
		}
		return (bHasAuthority || bSafe) ? State : nullptr;
	}

private:

	TUniqueFunction<void(bool bForWrite, TState*& OutState, bool& OutSafe, const TCHAR* TraceStr)> GetStateFunc;
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

// Helper to actual tick a simulation. This does all the setup, runs ::SimulationTick, and finalizes the frame.
// Caller is responsible to have the input buffers in the correct state: Input/Sync/Aux buffers must have valid data in OutputFrame-1.
// DeltaTime comes from the InputCmd->GetFrameDeltaTime().
// Important to note that this advances the PendingFrame to the output Frame. So that any writes that occur to the buffers during this scope will go to the output frame.
template<typename Model>
struct TSimulationDoTickFunc
{
	using TSimulation = typename Model::Simulation;
	using TTickSettings = typename Model::TickSettings;
	using TBufferTypes = typename Model::BufferTypes;

	using TDriver = TNetworkedSimulationModelDriver<TBufferTypes>;
	
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;
	
	using TFrameState = TSimulationFrameState<Model>;

	static void DoTick(TSimulation& Simulation, TNetworkedSimulationState<Model>& State, ESimulationTickContext Context)
	{
		check(State.GetTickInProgress() == false);
		const int32 InputFrame = State.GetPendingTickFrame();
		const int32 OutputFrame = State.GetPendingTickFrame()+1;

		// Get inputs. These all need to be present to tick (caller's responsibility)
		TFrameState* InFrameState = State.ReadFrame(InputFrame);
		const TAuxState* InAuxState = State.ReadAux(InputFrame);
		
		check(InFrameState);
		check(InAuxState);
		ensure(InFrameState->HasFlag(ESimulationFrameStateFlags::InputWritten));

		const FNetworkSimTime DeltaSimTime = InFrameState->FrameDeltaTime.Get();
		const FNetSimTimeStep SimTimeStep { DeltaSimTime, InFrameState->TotalSimTime, OutputFrame };
		const FNetworkSimTime OutSimTime = InFrameState->TotalSimTime + DeltaSimTime;

		TFrameState* OutFrameState = State.WriteFrameWithTime(OutputFrame, OutSimTime);

		// PreTick
		State.SetPendingTickFrame(OutputFrame);
		State.SetTickInProgress(true);
		State.CueDispatcher.PushContext({OutFrameState->TotalSimTime, Context}); // Cues "take place" at the end of the frame

		UE_NP_TRACE_SIM_TICK(OutputFrame, DeltaSimTime, SimTimeStep);

		// Simulation Tick
		Simulation.SimulationTick( SimTimeStep, // FNetSimTimeStep. How it ticks
			{ InFrameState->InputCmd, InFrameState->SyncState, *InAuxState }, // TNetSimInput
			{ OutFrameState->SyncState, State.GetAuxStateLazyWriter(OutputFrame), State.CueDispatcher } ); // TNetSimOutput

		// Post
		UE_NP_TRACE_USER_STATE_SYNC(OutFrameState->SyncState, OutputFrame);
		const TAuxState* AuxHead = State.ReadAux(OutputFrame);
		if (AuxHead != InAuxState)
		{
			UE_NP_TRACE_USER_STATE_AUX(*AuxHead, OutputFrame);
		}
				
		State.SetTickInProgress(false);
		State.CueDispatcher.PopContext();
	}
};
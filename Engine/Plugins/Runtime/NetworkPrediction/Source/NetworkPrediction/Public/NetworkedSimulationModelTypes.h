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
//		-TotalAllowedSimulationTime : time keeper for advancing simulation ticks
//	
//	-Frame Markers
//		-Pending		Next frame that will be ticked. Latest data.
//		-LatestInput	Latest frame to have input written to it. Can be ahead of pending (input buffering), equal to, or 1 frame behind.
//		-Confirmed		Last valid frame. Frames prior to confirmed frame do not need to be kept around.
//
//	The head frame of the SimulationState is Max(LatestInput, PendingTickFrame). LatestInput will be higher if you are buffering input.
//	Otherwise, PendingTickFrame will be up to one frame ahead of LatestInputFrame. They will be equal prior to running SimulatinTick.
//
//	This structure supports resizing up to a MaxCapacity size. It will resize in order to preserve the 3 frame markers (when valid).
//	E.g, if ConfirmedFrame=1 and we try to write Frame(1+BufferSize), the buffer will be resized so that ConfirmedFrame is preserved.
//	We cannot resize, we will crash! It is the calling code (RepController's) responsibility to handle this by checking with 
//	::GetMaxWritableFrame prior to calling a WriteFrame function.
//
//
//
// Examples
// Note that this is how the rep controllers currently work. But ultimately they are what drives the simulation forward
// and can set their own rules and own internal frame markers. 
//

//  Autonomous Proxy prior to running SimulationTick
//
//	Flags   TotalSimTime   DeltaTime   InputCmd   SyncState
// |------|--------------|-----------|----------|-----------|
// |   In |            X |        16 |        1 |         1 |	<-- Confirmed
// |   In |         X+16 |        20 |        1 |         1 |   
// |   In |         X+36 |        14 |        1 |         1 |	
// |   In |         X+50 |        17 |        1 |         1 |   <-- PendingTick + LatestInput
// |------|--------------|-----------|----------|-----------|


//	Autonomous Proxy after running SimulationTick:
//
//	Flags   TotalSimTime   DeltaTime   InputCmd   SyncState
// |------|--------------|-----------|----------|-----------|
// |   In |            X |        16 |        1 |         1 |	<-- Confirmed
// |   In |         X+16 |        20 |        1 |         1 |   
// |   In |         X+36 |        14 |        1 |         1 |
// |   In |         X+50 |        17 |        1 |         1 |   <-- LatestInput
// |    0 |         X+67 |         0 |        0 |         1 |   <-- PendingTick
// |------|--------------|-----------|----------|-----------|


// Authority w/ buffered input
//
//	Flags   TotalSimTime   DeltaTime   InputCmd   SyncState
// |------|--------------|-----------|----------|-----------|
// |   In |            X |        16 |        1 |         1 |	<-- PendingTick
// |   In |         X+16 |        20 |        1 |         0 |   
// |   In |         X+36 |        14 |        1 |         0 |	
// |    0 |            0 |         0 |        0 |         0 |   (Note gap frames! High packet loss caused these inputs to be missed)
// |    0 |            0 |         0 |        0 |         0 |  
// |   In |         X+50 |        17 |        1 |         0 |   <-- LatestInput
// |------|--------------|-----------|----------|-----------|

// ---------------------------------------------------------------------------------------------------------------------

// Specialize-able struct for setting initial/max size of frame buffer. This should be tied into network role eventually.
template<typename Model>
struct TNetworkedSimStateBufferSizes
{
	static constexpr int32 InitialSize() { return 16; }
	static constexpr int32 MaxSize() { return 128; }
};

template<typename Model>
struct TNetworkedSimulationState
{
	using TBufferTypes = typename Model::BufferTypes;
	using TickSettings = typename Model::TickSettings;

	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;

	TNetSimCueDispatcher<Model> CueDispatcher;

	TNetworkedSimulationState()
		: FrameBuffer(InitialCapactiy()), AuxBuffer(InitialCapactiy())
	{
	}

	int32 InitialCapactiy() const { return TNetworkedSimStateBufferSizes<Model>::InitialSize(); }
	int32 MaxCapacity() const { return TNetworkedSimStateBufferSizes<Model>::MaxSize(); }

	int32 GetCapacity() const { return FrameBuffer.Capacity(); }
	
	FString DebugString() const { return FString::Printf(TEXT("PendingTick: %d LatestInput: %d Size: %d. PT: %s. IT: %s. MT: %s"), 
		PendingTickFrame, LatestInputFrame, FrameBuffer.Capacity(), *GetTotalProcessedSimulationTime().ToString(), *GetTotalInputSimulationTime().ToString(), *GetTotalAllowedSimulationTime().ToString()); }

	// Highest frame that can be written without causing a system fault
	int32 GetMaxWritableFrame() const
	{
		return GetMaxWritableFrame(GetMinFrameMarker());
	}

	int32 GetMaxWritableFrame(const int32 MinFrameMarker) const
	{
		return MinFrameMarker + MaxCapacity() - 1;
	}
	
	int32 GetMinFrameMarker() const
	{
		// Calculate the lowest frame marker. Do this as uint32 to make INDEX_NONE values max
		return (int32)(FMath::Min<uint32>(FMath::Min<uint32>(ConfirmedFrame, PendingTickFrame), LatestInputFrame));
	}

	// Resets the FrameMarkers to NewPendingTickFrame. 
	void ResetToFrame(int32 NewPendingTickFrame, bool bHasInput)
	{
		PendingTickFrame = NewPendingTickFrame;
		ConfirmedFrame = NewPendingTickFrame;
		LatestInputFrame = bHasInput ? NewPendingTickFrame : INDEX_NONE;		
	}

	// Returns a frame for writing. This will always return a valid frame. Expected to be used with ResetToFrame when reseting the system.
	TSimulationFrameState<Model>* WriteFrameDirect(int32 Frame)
	{
		return &FrameBuffer[Frame];
	}
	
	// ---------------------------------------------------------------------
	// Frame State
	// ---------------------------------------------------------------------
	
	// Called to create the next frame for simulation ticking
	// Will return PendingTickFrame+1. Does not update frame markers.
	TSimulationFrameState<Model>* WriteNextTickFrame()
	{
		npEnsureMsgf(LatestInputFrame >= PendingTickFrame, TEXT("WriteNextTickFrame called with no input. %s"), *DebugString());
		const int32 NewFrame = PendingTickFrame+1;

		// Its possible the frame was already created (input buffering or resimulating)
		if (LatestInputFrame >= NewFrame)
		{
			return &FrameBuffer[NewFrame];
		}
		
		// It is the caller's responsibility to not write to an invalid frame. Use GetMaxWritableFrame() prior to calling.
		// The caller should then decide to either A) not write the frame or B) call ResetToFrame.
		const int32 MinFrameMarker = GetMinFrameMarker();
		npCheckf(NewFrame <= GetMaxWritableFrame(MinFrameMarker), TEXT("Invalid Frame Write. NewFrame: %d. %s"), NewFrame, *DebugString());		
		
		// Resize: we may need to resize the buffer to keep everything in the window.
		const int32 Delta = NewFrame - MinFrameMarker;

		if (Delta >= FrameBuffer.Capacity())
		{
			const int32 NewSize = Delta + 2; // will be rounded to next pow(2) in resize
			Resize(NewSize);
		}
		
		TSimulationFrameState<Model>& NewFrameState = FrameBuffer[NewFrame];
		NewFrameState.ClearFlags();
		return &NewFrameState;
	}

	// Writes a sequential frame to the buffer, even if it creates a gap. This is used in places where we want to maintain continuity of TotalSimTime for the new frame.
	// That is, regardless if frame# is not continuous, we want this new frame to have the correct TotalSimTime, based on the previous valid frame.
	//
	// If the frame already exists, will just return the frame
	// If this causes a new frame to be created, its flags will be cleared and TotalSimTime will be set for you. But contents will otherwise be stale
	// If a gap is created, those frames will also have flags cleared

	// Writes a new frame for input. 

	TSimulationFrameState<Model>* WriteInputFrame(int32 NewFrame)
	{
		npEnsureMsgf(NewFrame >= PendingTickFrame, TEXT("Input Frame write prior to PendingTickFrame. NewFrame: %d. %s"), NewFrame, *DebugString());
		npEnsureMsgf(NewFrame > LatestInputFrame, TEXT("Input Frame write prior to LatestInputFrame. NewFrame: %d. %s"), NewFrame, *DebugString());
		
		const int32 PrevFrame = FMath::Max(PendingTickFrame, LatestInputFrame);

		if (NewFrame > PendingTickFrame)
		{
			// It is the caller's responsibility to not write to an invalid frame. Use GetMaxWritableFrame() prior to calling.
			// The caller should then decide to either A) not write the frame or B) call ResetToFrame. We can't recover this deep so this is a check.
			const int32 MinFrameMarker = GetMinFrameMarker();
			npCheckf(NewFrame <= GetMaxWritableFrame(MinFrameMarker), TEXT("Invalid Frame Write. NewFrame: %d. %s"), NewFrame, *DebugString());	

			// Resize: we may need to resize the buffer to keep everything in the window.
			const int32 Delta = NewFrame - MinFrameMarker;
			if (Delta >= FrameBuffer.Capacity())
			{
				const int32 NewSize = Delta + 2; // will be rounded to next pow(2) in resize
				Resize(NewSize);
			}

			// Clear gap frame flags so they don't get processed
			for (int32 Frame = PrevFrame+1; Frame < NewFrame; ++Frame)
			{
				FrameBuffer[Frame].ClearFlags();
			}

			// We are creating a new frame so clear the flags
			FrameBuffer[NewFrame].ClearFlags();
		}

		// Look at the previous frame sequential frame and calculate the new Frame's TotalSimTime
		TSimulationFrameState<Model>& PrevFrameState = FrameBuffer[PrevFrame];
		TSimulationFrameState<Model>& NewFrameState = FrameBuffer[NewFrame];

		if (PrevFrameState.HasFlag(ESimulationFrameStateFlags::InputWritten))
		{
			NewFrameState.TotalSimTime = PrevFrameState.FrameDeltaTime.Get() + PrevFrameState.TotalSimTime;
		}
		else
		{
			NewFrameState.TotalSimTime = PrevFrameState.TotalSimTime;
		}

		npEnsureMsgf(!NewFrameState.HasFlag(ESimulationFrameStateFlags::InputWritten), TEXT("Input already written to Frame %d. %s"), NewFrame, *DebugString());
		NewFrameState.SetFlag(ESimulationFrameStateFlags::InputWritten);
		LatestInputFrame = NewFrame;
		
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
	
	// The last frame that had input written to it. This can be ahead of PendingTickFrame but never more than 1 frame behind.
	int32 GetLatestInputFrame() const { return LatestInputFrame; }
	void SetLatestInputFrame(int32 Frame)
	{
		LatestInputFrame = Frame;
		CheckInvariants(); 
	}

	// Where input is expected to be written to next. Gaps in NetSerialization can cause this to not be the case though.
	int32 GetNextInputFrame() const { return LatestInputFrame != INDEX_NONE ? LatestInputFrame+1 : PendingTickFrame; }

	void CheckInvariants()
	{
		npEnsureMsgf(ConfirmedFrame <= PendingTickFrame, TEXT("ConfirmedFrame %d <= PendingTickFrame"), ConfirmedFrame, PendingTickFrame);
		if (LatestInputFrame != INDEX_NONE)
		{
			npEnsureMsgf(PendingTickFrame <= LatestInputFrame+1, TEXT("PendingTickFrame %d <= (LatestInputFrame+1) %d"), PendingTickFrame, LatestInputFrame+1);
			npEnsureMsgf(IsValidFrame(LatestInputFrame), TEXT("LatestInputFrame not valid. %s"), *DebugString());
			npEnsureMsgf(FrameBuffer[LatestInputFrame].HasFlag(ESimulationFrameStateFlags::InputWritten), TEXT("LatestInputFrame doesnt actually have input %s"), *DebugString());
		}

#if NP_CHECKS_AND_ENSURES_SLOW
		// If PendingTickFrame is ahead of input, ensure that calculated TotalSimTime is what we expected.
		// If this fires, its likely LatestInputFrame is wrong. If we took a frame from a NetSerialize for example, latest InputFrame should be reset.
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
	int32 TailFrame() const { return HeadFrame() - FrameBuffer.Capacity() + 1; }
	
	bool IsValidFrame(int32 Frame) const
	{
		const int32 Head = FMath::Max(PendingTickFrame, LatestInputFrame);
		const int32 Tail = Head - FrameBuffer.Capacity() + 1;
		return Frame >= TailFrame() && Frame <= HeadFrame();
	}

	const TSimulationFrameState<Model>* GetValidFrame(int32 Frame) const
	{
		return IsValidFrame(Frame) ? &FrameBuffer[Frame] : nullptr;
	}

	TSimulationFrameState<Model>* GetValidFrame(int32 Frame)
	{
		return IsValidFrame(Frame) ? &FrameBuffer[Frame] : nullptr;
	}

	void Resize(int32 NewSize)
	{
		FrameBuffer.Resize(NewSize, HeadFrame());
		AuxBuffer.Resize(NewSize);
	}

	bool bTickInProgress = false;

	int32 PendingTickFrame = 0;
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
// Caller is responsible for ensuring State.PendingTickFrame() has valid input and that State.PendingTickFrame()+1 <= State.GetMaxWritableFrame()
//		(E.g, don't call this is it would age out ConfirmedFrame!)
//
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

		// Write the output frame (important to do before caching internal data since the write could cause a resize)
		TFrameState* OutFrameState = State.WriteNextTickFrame();

		// Get inputs. These all need to be present to tick (caller's responsibility)
		TFrameState* InFrameState = State.ReadFrame(InputFrame);
		const TAuxState* InAuxState = State.ReadAux(InputFrame);
		
		check(InFrameState);
		check(InAuxState);
		npEnsureMsgf(InFrameState->HasFlag(ESimulationFrameStateFlags::InputWritten), TEXT("DoTick called with invalid InputFrame: %d. %s"), InputFrame, *State.DebugString());

		const FNetworkSimTime DeltaSimTime = InFrameState->FrameDeltaTime.Get();
		const FNetSimTimeStep SimTimeStep { DeltaSimTime, InFrameState->TotalSimTime, OutputFrame };
		OutFrameState->TotalSimTime = InFrameState->TotalSimTime + DeltaSimTime;
		
		npCheckf(OutputFrame <= State.GetMaxWritableFrame(), TEXT("DoTick called on invalid OutputFrame: %d. %s"), OutputFrame, *State.DebugString());

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
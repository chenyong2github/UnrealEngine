// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Net/UnrealNetwork.h" // For MakeRelative
#include "NetworkedSimulationModelCVars.h"
#include "NetworkedSimulationModelInterpolator.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "NetworkPredictionCheck.h"

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	Replication Controllers
//	
//	The RepControllers are the pieces of the TNetworkedSimulationModel that make up the role-specific functionality (Server, Autonomous Client, and Simulated Client).
//	Mainly they NetSerialize, Reconcile, and Tick. Essentially, they are what drives the Networked Simulation frame to frame based on networking role.
//
//	Multiple controllers can be used on a single TNetworkedSimulationModel instance. There is a 1:1 mapping for Tick, Reconcile, and Receiving. But to send, the 
//	RepController of the receiving side is used.
//
//	Tick, Reconcile, NetSerialize: Receive
//		Server    --> TRepController_Server
//		AP Client --> TRepController_Autonomous
//		SP Client --> TRepController_Simulated
//
//	NetSerialize: Send
//		Server    --> TRepController_Autonomous, TRepController_Simulated
//		AP Client --> TRepController_Server
//
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace NetworkSimulationModelCVars
{
	NETSIM_DEVCVAR_SHIPCONST_INT(EnableLocalPrediction, 1, "ns.EnableLocalPrediction", "Toggle local prediction.");
	NETSIM_DEVCVAR_SHIPCONST_INT(EnableSimulatedReconcile, 1, "ns.EnableSimulatedReconcile", "Toggle simulated proxy reconciliation.");
	NETSIM_DEVCVAR_SHIPCONST_INT(EnableSimulatedExtrapolation, 1, "ns.EnableSimulatedExtrapolation", "Toggle simulated proxy extrapolation.");
	NETSIM_DEVCVAR_SHIPCONST_INT(ForceReconcile, 0, "ns.ForceReconcile", "Forces reconcile even if state does not differ. E.g, force resimulation after every netupdate.");
	NETSIM_DEVCVAR_SHIPCONST_INT(ForceReconcileSingle, 0, "ns.ForceReconcileSingle", "Forces a since reconcile to happen on the next frame");
}

#define NETSIM_ENABLE_CHECKSUMS !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#if NETSIM_ENABLE_CHECKSUMS 
#define NETSIM_CHECKSUM(Ser) SerializeChecksum(Ser,0xA186A384, false);
#else
#define NETSIM_CHECKSUM
#endif

#ifndef NETSIM_NETCONSTANT_NUM_BITS_FRAME
	#define NETSIM_NETCONSTANT_NUM_BITS_FRAME 8	// Allows you to override this setting via UBT, but access via cleaner FActorMotionNetworkingConstants::NUM_BITS_FRAME
#endif

struct FNetworkSimulationSerialization
{
	// How many bits we use to NetSerialize Frame numbers. This is only relevant AP Client <--> Server communication.
	// Frames are stored locally as 32 bit integers, but we use a smaller number of bits to NetSerialize.
	// The system internally guards from Frame numbers diverging. E.g, the client will not generate new frames if the
	// last serialization frame would be pushed out of the buffer. Server does not generate frames without input from client.
	enum { NUM_BITS_FRAME = NETSIM_NETCONSTANT_NUM_BITS_FRAME };

	// Abs max value we encode into the bit writer
	enum { MAX_FRAME_WRITE = 1 << NUM_BITS_FRAME };

	// This is the threshold at which we would wrap around and incorrectly assign a frame on the receiving side.
	// E.g, If there are FRAME_ERROR_THRESHOLD frames that do not make it across from sender->receiver, the
	// receiver will have incorrect local values. With 8 bits, this works out to be 128 frames or about 2 seconds at 60fps.
	enum { FRAME_ERROR_THRESHOLD = MAX_FRAME_WRITE / 2};

	// Helper to serialize the int32 HeadFrame. Returns the unpacked value (this will be same as input in the save path)
	static int32 SerializeFrame(FArchive& Ar, int32 Frame, int32 LastSerializedFrame)
	{
		if (Ar.IsSaving())
		{
			((FNetBitWriter&)Ar).WriteIntWrapped( Frame, MAX_FRAME_WRITE );
			return Frame;
		}
		
		return MakeRelative(((FNetBitReader&)Ar).ReadInt( MAX_FRAME_WRITE ), LastSerializedFrame, MAX_FRAME_WRITE );
	}
};

// -------------------------------------------------------------------------------------------------------
//	Helpers
// -------------------------------------------------------------------------------------------------------

/** Helper that writes a new input cmd to the input buffer, at given Frame (usually the sim's PendingFrame). If frame doesn't exist, ProduceInput is called on the driver if bProduceInputViaDriver=true, otherwise the input cmd will be initialized from the previous input cmd. */
template<typename Model>
void GenerateLocalInputCmdAtFrame(TNetworkedSimulationModelDriver<typename Model::BufferTypes>* Driver, TNetworkedSimulationState<Model>& State, const FNetworkSimTime& DeltaSimTime, bool bProduceInputViaDriver)
{
	// Write the new frame
	const int32 InputFrame = State.GetNextInputFrame();
	if (InputFrame > State.GetMaxWritableFrame())
	{
		return;
	}

	TSimulationFrameState<Model>* FrameState = State.WriteInputFrame(InputFrame);
	npCheckSlow(FrameState);	
	FrameState->FrameDeltaTime.Set(DeltaSimTime);

	if (bProduceInputViaDriver)
	{
		UE_NP_TRACE_PRODUCE_INPUT();
		Driver->ProduceInput(DeltaSimTime, FrameState->InputCmd);
	}
	else
	{
		UE_NP_TRACE_SYNTH_INPUT();
	}
	
	UE_NP_TRACE_USER_STATE_INPUT(FrameState->InputCmd, InputFrame);
}

/** Helper to generate a local input cmd if we have simulation time to spend and advance the simulation's MaxAllowedFrame so that it can be processed. */
template<typename Model>
void TryGenerateLocalInput(TNetworkedSimulationModelDriver<typename Model::BufferTypes>* Driver, TNetworkedSimulationState<Model>& State, bool bProduceInputViaDriver)
{	
	const FNetworkSimTime AllowedSimTime = State.GetTotalAllowedSimulationTime();
	const int32 MaxInputFrame = State.GetMaxWritableFrame();
	
	// Submit new input if necessary
	FNetworkSimTime MissingInputTime = AllowedSimTime - State.GetTotalInputSimulationTime();
	while (MissingInputTime.IsPositive())
	{
		const int32 InputFrame = State.GetNextInputFrame();
		if (InputFrame >= MaxInputFrame)
		{
			break;
		}
		
		TSimulationFrameState<Model>* FrameState = State.WriteInputFrame(InputFrame);
		npCheckSlow(FrameState);		
		FrameState->FrameDeltaTime.Set(MissingInputTime);

		npEnsureMsgfSlow(State.GetTotalAllowedSimulationTime() >= State.GetTotalInputSimulationTime(), TEXT("Overflowed input %s"), *State.DebugString());
		MissingInputTime -= FrameState->FrameDeltaTime.Get();

		if (bProduceInputViaDriver)
		{
			UE_NP_TRACE_PRODUCE_INPUT();
			Driver->ProduceInput(FrameState->FrameDeltaTime.Get(), FrameState->InputCmd);
		}
		else
		{
			UE_NP_TRACE_SYNTH_INPUT();
		}
		
		UE_NP_TRACE_USER_STATE_INPUT(FrameState->InputCmd, InputFrame);
	}
}

template<typename Model>
void DoSimulationTicks(typename Model::Simulation& Simulation, TNetworkedSimulationState<Model>& State, ESimulationTickContext Context, const int32 MaxTickFrame)
{
	using TFrameState = TSimulationFrameState<Model>;

	while (State.GetPendingTickFrame() <= MaxTickFrame)
	{
		const int32 InputSyncFrame = State.GetPendingTickFrame(); // Where Sync/Aux state have to come from. PendingFrame is where OOB mods are made, so we cannot skip ahead.
			
		// Find InputFrame. We want it to be PendingFrame but if no input was written at this frame, it can be skipped if there are valid inputs at later frames (gaps in network recvs)
		int32 InputFrame = InputSyncFrame;
		TFrameState* InputFrameState = State.ReadFrame(InputFrame);
		while (InputFrameState && !InputFrameState->HasFlag(ESimulationFrameStateFlags::InputWritten))
		{
			if (++InputFrame > State.GetLatestInputFrame())
			{
				InputFrameState = nullptr;
				break;
			}
			InputFrameState = State.ReadFrame(InputFrame);
		}

		// No valid input to be processed between PendingFrame -> MaxAllowedFrame
		if (!InputFrameState)
		{
			break;
		}

		// If we skipped frames due to lack of input, we need to copy the sync state over and set PendingTickFrame
		if (InputFrame > InputSyncFrame)
		{
			TSimulationFrameState<Model>* InputSyncState = State.ReadFrame(InputSyncFrame);
			InputFrameState->SyncState = InputSyncState->SyncState;
			InputFrameState->TotalSimTime = InputSyncState->TotalSimTime;
			State.SetPendingTickFrame(InputFrame);
		}

		const FNetworkSimTime EndSimTime = InputFrameState->TotalSimTime + InputFrameState->FrameDeltaTime.Get();
		if (EndSimTime <= State.GetTotalAllowedSimulationTime())
		{
			TSimulationDoTickFunc<Model>::DoTick(Simulation, State, Context);
		}
		else
		{
			break;
		}
	}
}

// Empty RepController: zero functionality, just stubbed out functions and using definitions for convenience
template<typename Model>
struct TRepController_Empty
{ 
	using TSimulation = typename Model::Simulation;
	using TTickSettings = typename Model::TickSettings;
	using TBufferTypes = typename Model::BufferTypes;

	using TDriver = TNetworkedSimulationModelDriver<TBufferTypes>;
	using TFrameState = TSimulationFrameState<Model>;
	
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;

	// NetSerialize: just serialize the network data. Don't run simulation steps. Every replicator will be NetSerialized before moving on to Reconcile phase.
	void NetSerialize(const FNetSerializeParams& P, TNetworkedSimulationState<Model>& State) { }

	// Reconcile: called after everyone has NetSerialized. "Get right with the server": this function is about reconciling what the server told you vs what you have extrapolated or forward predicted locally
	void Reconcile(TSimulation* Sim, TDriver* Driver, TNetworkedSimulationState<Model>& State) { }

	// Called prior to input processing. This function must update tick state to allow simulation time (from TickParameters) and to possibly get new input.
	void Tick(TDriver* Driver, TNetworkedSimulationState<Model>& Buffers, const FNetSimTickParameters& TickParameters) { }
};

// ----------------------------------------------------------------------------------------------------------------------------------------
//	TRepController_Server
//	Server Replication Controller. Server receives and ticks on this controller. AutonomousProxy sends on this controller.
// ----------------------------------------------------------------------------------------------------------------------------------------
template<typename Model, int32 NumSendPerUpdate=3, typename TBase=TRepController_Empty<Model>>
struct TRepController_Server : public TBase
{
	using typename TBase::TSimulation;
	using typename TBase::TTickSettings;
	using typename TBase::TDriver;
	using typename TBase::TFrameState;
	using typename TBase::TInputCmd;
	using typename TBase::TSyncState;
	using typename TBase::TAuxState;
	
	// -------------------------------------------------------------------------------------------------
	// TRepController_Server::NetSerialize
	//	This is the client sending input data (Ar.IsSaving) to the server receiving input data (Ar.IsLoading)
	// -------------------------------------------------------------------------------------------------
	void NetSerialize(const FNetSerializeParams& P, TNetworkedSimulationState<Model>& State)
	{
		NETSIM_CHECKSUM(P.Ar);

		FArchive& Ar = P.Ar;

		// Serialize Input Cmds redundantly. This could be better:
		//	-Sending: We could delta compress the data that doesn't change in subsequent cmds
		//	-Receiving: we could skip cmds that we already have (right now we must NetSerialize redundantly, we can't skip ahead in the stream)
		
		if (Ar.IsSaving())
		{
			FNetBitWriter& BitWriter = (FNetBitWriter&)P.Ar;

			const int32 HeadInputFrame = State.GetLatestInputFrame();
			const int32 StartInputFrame = FMath::Max(0, (HeadInputFrame - NumSendPerUpdate + 1));
			
			FBitWriterMark NumSentMark(BitWriter);

			uint8 SerializedNumElements = 0;
			Ar << SerializedNumElements;

			FNetworkSimulationSerialization::SerializeFrame(Ar, HeadInputFrame, LastSerializedFrame);

			for (int32 Frame = StartInputFrame; Frame <= HeadInputFrame; ++Frame)
			{
				TFrameState* FrameState = State.ReadFrame(Frame);
				if (FrameState && FrameState->HasFlag(ESimulationFrameStateFlags::InputWritten))
				{
					SerializedNumElements++;

					FrameState->FrameDeltaTime.NetSerialize(P.Ar);
					FrameState->InputCmd.NetSerialize(P);
				}
			}

			FBitWriterMark Restore(BitWriter);
			NumSentMark.PopWithoutClear(BitWriter);
			Ar << SerializedNumElements;
			Restore.PopWithoutClear(BitWriter);

			LastSerializedFrame = HeadInputFrame;
		}
		else
		{
			auto TraceNewInputCmd = [](TInputCmd& Cmd, const FNetworkSimTime& TotalSimTime, const int32& Frame)
			{
				UE_NP_TRACE_NET_SERIALIZE_RECV(TotalSimTime, Frame);
				UE_NP_TRACE_USER_STATE_INPUT(Cmd, Frame);
				UE_NP_TRACE_NET_SERIALIZE_COMMIT();
			};

			TInputCmd ScratchInputCmd;
			FNetworkSimTime ScratchNetworkSimTime;

			uint8 SerializedNumElements = 0;
			Ar << SerializedNumElements;

			SerializedNumElements = FMath::Clamp<uint8>(SerializedNumElements, 0, NumSendPerUpdate); // Don't let client send us garbage

			const int32 ReceivedHeadInputFrame = FMath::Max(FNetworkSimulationSerialization::SerializeFrame(Ar, State.GetLatestInputFrame(), LastSerializedFrame), 0);
			const int32 StartInputFrame = FMath::Max(ReceivedHeadInputFrame - SerializedNumElements + 1, 0);

			const int32 MinAcceptableFrame = State.GetNextInputFrame();
			const int32 MaxAcceptableFrame = State.GetMaxWritableFrame();

			for (int32 Frame = StartInputFrame; Frame <= ReceivedHeadInputFrame; ++Frame)
			{
				if (Frame < MinAcceptableFrame || Frame > MaxAcceptableFrame)
				{
					// We don't want this frame. We've either moved past where it could matter or it is a redundant receive, or its so far ahead in the future that it would overwrite our buffer.
					ScratchNetworkSimTime.NetSerialize(P.Ar);
					ScratchInputCmd.NetSerialize(P);
					
					//UE_NP_TRACE_SYSTEM_FAULT("TRepController_Server::NetSerialize received Frame %d outside of acceptable range %d-%d.", Frame, MinAcceptableFrame, MaxAcceptableFrame);
					continue;
				}
				else
				{
					LastSerializedFrame = Frame;
				}

				if (TFrameState* ExistingFrame = State.ReadFrame(Frame))
				{
					if (npEnsure(!ExistingFrame->HasFlag(ESimulationFrameStateFlags::InputWritten)))
					{
						// Recv input on frame we've created (but haven't written input to)
						ExistingFrame->FrameDeltaTime.NetSerialize(P.Ar);
						ExistingFrame->InputCmd.NetSerialize(P);
						ExistingFrame->SetFlag(ESimulationFrameStateFlags::InputWritten);

						TraceNewInputCmd(ExistingFrame->InputCmd, ExistingFrame->TotalSimTime, Frame);

						State.SetLatestInputFrame(Frame);
					}
				}
				else
				{
					// Brand new frame
					TFrameState* NewFrameState = State.WriteInputFrame(Frame);
					npCheckSlow(NewFrameState);
					
					NewFrameState->FrameDeltaTime.NetSerialize(P.Ar);
					NewFrameState->InputCmd.NetSerialize(P);					

					TraceNewInputCmd(NewFrameState->InputCmd, NewFrameState->TotalSimTime, Frame);
				}
			}
		}
		
		NETSIM_CHECKSUM(P.Ar);
	}
	
	// -------------------------------------------------------------------------------------------------
	// TRepController_Server::Reconcile
	//	This is the server reconciling the simulation state with what the client sent
	// -------------------------------------------------------------------------------------------------
	void Reconcile(TSimulation* Simulation, TDriver* Driver, TNetworkedSimulationState<Model>& State)
	{
		// Nothing to actually reconcile server side
	}
	
	// -------------------------------------------------------------------------------------------------
	// TRepController_Server::Tick
	//	This is the server ticking locally
	// -------------------------------------------------------------------------------------------------
	void Tick(TDriver* Driver, TSimulation* Simulation, TNetworkedSimulationState<Model>& State, const FNetSimTickParameters& TickParameters)
	{
		State.GiveSimulationTime(TickParameters.LocalDeltaTimeSeconds);
		if (TickParameters.bGenerateLocalInputCmds)
		{
			TryGenerateLocalInput(Driver, State, true);
		}

		DoSimulationTicks(*Simulation, State, ESimulationTickContext::Authority, State.GetLatestInputFrame());

		// Sync to latest frame if there is any
		const int32 PendingFrame = State.GetPendingTickFrame();
		Driver->FinalizeFrame(State.ReadFrame(PendingFrame)->SyncState, *State.ReadAux(PendingFrame));
	}

	int32 GetLastSerializedFrame() const { return LastSerializedFrame; }

private:
	
	int32 LastSerializedFrame = -1;

};

// ----------------------------------------------------------------------------------------------------------------------------------------
//	TRepController_Autonomous
//	Predictive client: reconciles with received authoritative state
// ----------------------------------------------------------------------------------------------------------------------------------------
template<typename Model, typename TBase=TRepController_Empty<Model>>
struct TRepController_Autonomous: public TBase
{
	using typename TBase::TSimulation;
	using typename TBase::TTickSettings;
	using typename TBase::TDriver;
	using typename TBase::TFrameState;
	using typename TBase::TInputCmd;
	using typename TBase::TSyncState;
	using typename TBase::TAuxState;

	int32 GetLastSerializedFrame() const { return SerializedFrame; }
	bool IsReconcileFaultDetected() const { return bReconcileFaultDetected; }
	const FNetworkSimTime& GetLastSerializedSimTime() const { return SerializedTime; }

	TArray<INetworkedSimulationModel*> DependentSimulations;
	bool bDependentSimulationNeedsReconcile = false;

	// --------------------------------------------------------------------
	//	TRepController_Autonomous::NetSerialize
	// --------------------------------------------------------------------
	void NetSerialize(const FNetSerializeParams& P, TNetworkedSimulationState<Model>& State)
	{
		NETSIM_CHECKSUM(P.Ar);
		FArchive& Ar = P.Ar;
		
		SerializedFrame = FNetworkSimulationSerialization::SerializeFrame(Ar, State.GetPendingTickFrame(), SerializedFrame);
		
		SerializedTime = State.GetTotalProcessedSimulationTime();
		SerializedTime.NetSerialize(P.Ar);

		if (Ar.IsSaving())
		{
			// Server serialize the latest state
			State.ReadFrame(SerializedFrame)->SyncState.NetSerialize(Ar);
			State.ReadAux(SerializedFrame)->NetSerialize(Ar);
			State.CueDispatcher.NetSerializeSavedCues(Ar, ENetSimCueReplicationTarget::AutoProxy);
		}
		else
		{
			SerializedSyncState.NetSerialize(Ar);
			SerializedAuxState.NetSerialize(Ar);
			State.CueDispatcher.NetSerializeSavedCues(Ar, ENetSimCueReplicationTarget::AutoProxy);

			UE_NP_TRACE_NET_SERIALIZE_RECV(SerializedTime, SerializedFrame);
			UE_NP_TRACE_USER_STATE_SYNC(SerializedSyncState, SerializedFrame);
			UE_NP_TRACE_USER_STATE_AUX(SerializedAuxState, SerializedFrame);
			
			bPendingReconciliation = false;

			TFrameState* ClientFrameState = State.ReadFrame(SerializedFrame);
			TAuxState* ClientAuxState = State.ReadAux(SerializedFrame);

			// The state the client predicted that corresponds to the state the server just serialized to us
			if (ClientFrameState && ClientAuxState)
			{
				const bool bForceReconcile = (NetworkSimulationModelCVars::ForceReconcile() > 0) || (NetworkSimulationModelCVars::ForceReconcileSingle() > 0);
				
				if (bForceReconcile || ClientFrameState->TotalSimTime != SerializedTime || Model::ShouldReconcile(SerializedSyncState, SerializedAuxState, ClientFrameState->SyncState, *ClientAuxState))
				{
					NetworkSimulationModelCVars::SetForceReconcileSingle(0);
					bPendingReconciliation =  true;
				}
			}
			else
			{
				// We don't have this frame locally anymore, will sort it out in Reconcile
				bPendingReconciliation =  true;
				UE_NP_TRACE_SYSTEM_FAULT("TRepController_Autonomous::NetSerialize Unable to compare SerializedState. Received: %d. %s", SerializedFrame, *State.DebugString());
			}

			if (!bPendingReconciliation)
			{
				State.SetConfirmedFrame(SerializedFrame);
			}
		}
		NETSIM_CHECKSUM(P.Ar);
	}

	// --------------------------------------------------------------------
	//	TRepController_Autonomous::Reconcile
	// --------------------------------------------------------------------
	void Reconcile(TSimulation* Simulation, TDriver* Driver, TNetworkedSimulationState<Model>& State)
	{
		if (bPendingReconciliation == false && (bDependentSimulationNeedsReconcile == false || SerializedFrame == INDEX_NONE))
		{
			return;
		}

		UE_NP_TRACE_NET_SERIALIZE_COMMIT();
		bPendingReconciliation = false;
		bDependentSimulationNeedsReconcile = false;

		TFrameState* ClientFrameState = State.ReadFrame(SerializedFrame);
		if (!ClientFrameState)
		{
			// We received state that we don't have in our local buffer.
			// We can't resimulate here, just reset to the serialized state
			UE_NP_TRACE_SYSTEM_FAULT("Could not reconcile SerializedFrame %d with local state. %s", SerializedFrame, *State.DebugString());
			ResetToSerializedState(State);
			return;
		}
		
		FNetworkSimTime RollbackDeltaTime = ClientFrameState->TotalSimTime - SerializedTime;

		// Commit client's state to the server version
		ClientFrameState->SyncState = SerializedSyncState;
		ClientFrameState->TotalSimTime = SerializedTime;
		*State.WriteAuxState(SerializedFrame) = SerializedAuxState;

		if (NetworkSimulationModelCVars::EnableLocalPrediction() == 0)
		{
			// If we aren't predicting at all, then we advanced the allowed sim time here, (since we aren't doing it in PreSimTick). This just keeps us constantly falling behind and not being able to toggle prediction on/off for debugging.
			State.SetTotalAllowedSimulationTime(SerializedTime);
		}

		// Tell dependent simulations to rollback
		for (INetworkedSimulationModel* DependentSim : DependentSimulations)
		{
			DependentSim->BeginRollback(RollbackDeltaTime, SerializedFrame);
		}

		// Tell cue dispatch that we've rolledback as well
		State.CueDispatcher.NotifyRollback(SerializedTime);

		const int32 StartingPendingTickFrame = State.GetPendingTickFrame();
		const int32 LastResimulateFrame = StartingPendingTickFrame-1;
		if (SerializedFrame < StartingPendingTickFrame)
		{
			VisualLogFrameStates(Driver, State, SerializedFrame, LastResimulateFrame-1, EVisualLoggingContext::OtherMispredicted, TEXT("Resimulate Step: mispredicted"));
			VisualLogFrameStates(Driver, State, LastResimulateFrame, LastResimulateFrame, EVisualLoggingContext::LastMispredicted, TEXT("Resimulate Step: final mispredicted"));

			FNetworkSimTime StartingRemainingTime = State.GetTotalAllowedSimulationTime() - State.GetTotalProcessedSimulationTime();
			npEnsureMsgf(!StartingRemainingTime.IsNegative(), TEXT("Negative remaining SimTime %s %s"), *State.GetTotalAllowedSimulationTime().ToString(), *State.GetTotalProcessedSimulationTime().ToString());

			State.SetPendingTickFrame(SerializedFrame);

			DoSimulationTicks(*Simulation, State, ESimulationTickContext::Resimulate, StartingPendingTickFrame-1);
			
			State.SetTotalAllowedSimulationTime(State.GetTotalProcessedSimulationTime() + StartingRemainingTime);

			VisualLogFrameStates(Driver, State, SerializedFrame, LastResimulateFrame-1, EVisualLoggingContext::OtherPredicted, TEXT("Resimulate Step: repredicted"));
			VisualLogFrameStates(Driver, State, LastResimulateFrame, LastResimulateFrame, EVisualLoggingContext::LastPredicted, TEXT("Resimulate Step: final repredicted"));
		}
		else
		{
			// We have no frames to resimulate, essentially we are reset to the SerializedFrame	
			State.ResetToFrame(SerializedFrame, ClientFrameState->HasFlag(ESimulationFrameStateFlags::InputWritten));
			State.SetTotalAllowedSimulationTime(ClientFrameState->TotalSimTime);
		}

		State.CheckInvariants();
	}

	// --------------------------------------------------------------------
	//	TRepController_Autonomous::Tick
	// --------------------------------------------------------------------
	void Tick(TDriver* Driver, TSimulation* Simulation, TNetworkedSimulationState<Model>& State, const FNetSimTickParameters& TickParameters)
	{
		if (SerializedFrame == INDEX_NONE)
		{
			State.SetConfirmedFrame(0);
		}

		// The MaxInputFrame we can write without getting too far ahead of server
		// We pass this into TryGenerateLocalInput to ensure that it doesn't.
		// If we some how get in a state where we are past MaxInputFrame, we need to reset
		const int32 MaxInputFrame = State.GetMaxWritableFrame();

		if (State.GetNextInputFrame() > MaxInputFrame)
		{
			// We somehow went over the limit and must reset.
			ResetToSerializedState(State);
			UE_NP_TRACE_SYSTEM_FAULT("TRepController_Autonomous::PreSimTick input overrun. MaxInputFrame: %d. %s", MaxInputFrame, *State.DebugString());
			return;
		}

		if (State.GetNextInputFrame() == MaxInputFrame)
		{
			// We are at the limit but not over it. We are essentially pausing the simulation until we recover. Set this bool to reflect this.
			bReconcileFaultDetected = true;
			UE_NP_TRACE_SYSTEM_FAULT("TRepController_Autonomous::PreSimTick Input Buffer Maxed. Not processing local time. MaxInputFrame: %d. %s", MaxInputFrame, *State.DebugString());
			return;
		}

		bReconcileFaultDetected = false;

		if (TickParameters.bGenerateLocalInputCmds)
		{
			if (NetworkSimulationModelCVars::EnableLocalPrediction() > 0)
			{
				// Prediction: add simulation time and generate new commands
				State.GiveSimulationTime(TickParameters.LocalDeltaTimeSeconds);
				TryGenerateLocalInput<Model>(Driver, State, true);
			}
			else
			{
				// Local prediction disabled: we must use a separate time accumulator to figure out when we should add more input cmds.
				// Since we aren't processing the simulation locally, our core simulation time will only advance from network updates.
				// (still, we need *something* to tell us when to generate a new command and what the delta time should be)
				FNetworkSimTime NonPredictedInputTime;
				NonPredictedInputTimeAccumulator.Accumulate(NonPredictedInputTime, TickParameters.LocalDeltaTimeSeconds);
				if (NonPredictedInputTime.IsPositive())
				{
					GenerateLocalInputCmdAtFrame<Model>(Driver, State, NonPredictedInputTime, true);
				}
			}
		}

		DoSimulationTicks(*Simulation, State, ESimulationTickContext::Predict, State.GetLatestInputFrame());

		State.CueDispatcher.ClearMaxDispatchTime();
		State.CueDispatcher.SetConfirmedTime(SerializedTime);

		const int32 PendingFrame = State.GetPendingTickFrame();
		Driver->FinalizeFrame(State.ReadFrame(PendingFrame)->SyncState, *State.ReadAux(PendingFrame));
	}

private:
	
	// Called as last resort to full reset to last serialized state. Commands in flight will cause correction
	void ResetToSerializedState(TNetworkedSimulationState<Model>& State)
	{
		if (SerializedFrame != INDEX_NONE)
		{
			TFrameState* Frame = State.WriteFrameDirect(SerializedFrame);
			Frame->TotalSimTime = SerializedTime;
			Frame->SyncState = SerializedSyncState;

			State.ResetToFrame(SerializedFrame, Frame->HasFlag(ESimulationFrameStateFlags::InputWritten));
			State.SetTotalAllowedSimulationTime(Frame->TotalSimTime);

			*State.WriteAuxState(SerializedFrame) = SerializedAuxState;			
		}
	}

	void VisualLogFrameStates(TDriver* Driver, TNetworkedSimulationState<Model>& State, int32 StartFrame, int32 EndFrame, EVisualLoggingContext Context, const TCHAR* Str)
	{
		if (NETSIM_MODEL_DEBUG)
		{
			for (int32 Frame = StartFrame; Frame <= EndFrame; ++Frame)
			{
				TFrameState* FrameState = State.ReadFrame(Frame);
				if (npEnsureMsgf(FrameState, TEXT("Invalid Frame %d. %s"), Frame, *State.DebugString()))
				{
					FVisualLoggingParameters VLogParameters(Context, Frame, EVisualLoggingLifetime::Persistent, Str);
					Driver->InvokeVisualLog(&FrameState->InputCmd, &FrameState->SyncState, State.ReadAux(Frame), VLogParameters);
				}
			}
		}
	}
	
	TSyncState SerializedSyncState;
	TAuxState SerializedAuxState;
	FNetworkSimTime SerializedTime;
	int32 SerializedFrame = INDEX_NONE;

	bool bPendingReconciliation = false;	// Reconciliation is pending: we need to reconcile state from the server that differs from the locally predicted state
	bool bReconcileFaultDetected = false;	// A fault was detected: we received state from the server that we are unable to reconcile with locally predicted state

	TRealTimeAccumulator<TTickSettings> NonPredictedInputTimeAccumulator; // for tracking input time in the non predictive case
};

// ----------------------------------------------------------------------------------------------------------------------------------------
//	TRepController_Simulated
//	Clients receive and tick on this controller for non locally controlled simulations. Server sends on controller to those clients.
//	Can run in three modes:
//		-Interpolate: no running on SimulationTick
//		-SimulationExtrapolation: run SimulationTick in between network updates
//		-ForwardPredict: forward predict in step with a leading Autonomous Proxy simulation
// ----------------------------------------------------------------------------------------------------------------------------------------
template<typename Model, typename TBase=TRepController_Empty<Model>>
struct TRepController_Simulated : public TBase
{
	using typename TBase::TSimulation;
	using typename TBase::TTickSettings;
	using typename TBase::TDriver;
	using typename TBase::TFrameState;
	using typename TBase::TInputCmd;
	using typename TBase::TSyncState;
	using typename TBase::TAuxState;

	// Parent Simulation. If this is set, this simulation will forward predict in sync with this parent sim. The parent sim should be an autonomous proxy driven simulation
	INetworkedSimulationModel* ParentSimulation = nullptr;

	// Instance flag for enabling simulated extrapolation
	bool bAllowSimulatedExtrapolation = true;

	// Interpolated that will be used if bAllowSimulatedExtrapolation == false && ParentSimulation == nullptr
	TNetSimInterpolator<Model> Interpolator;

	// Last Serialized time and state (this is for the debugger which should go away)
	FNetworkSimTime GetLastSerializedSimulationTime() const { return LastSerializedSimulationTime; }
	const TSyncState& GetLastSerializedSyncState() const { return LastSerializedSyncState; }
	const TAuxState& GetLastSerializedAuxState() const { return LastSerializedAuxState; }
	const TInputCmd& GetLastSerializedInputCmd() const { return LastSerializedInputCmd; }
	int32 GetLastSerializedFrame() const { return LastSerializedKeyframe; }
	
	ESimulatedUpdateMode GetSimulatedUpdateMode() const
	{
		if (ParentSimulation)
		{
			return ESimulatedUpdateMode::ForwardPredict;
		}
		if (bAllowSimulatedExtrapolation && NetworkSimulationModelCVars::EnableSimulatedExtrapolation())
		{
			return ESimulatedUpdateMode::Extrapolate;
		}

		return ESimulatedUpdateMode::Interpolate;
	}

	// -------------------------------------------------------------------------------------------------
	// TRepController_Simulated::NetSerialize
	// -------------------------------------------------------------------------------------------------
	void NetSerialize(const FNetSerializeParams& P, TNetworkedSimulationState<Model>& State)
	{
		FArchive& Ar = P.Ar;
		NETSIM_CHECKSUM(P.Ar);

		if (Ar.IsSaving())
		{
			LastSerializedKeyframe = State.GetPendingTickFrame();
			
			TFrameState* FrameState = State.ReadFrame(LastSerializedKeyframe);
			npCheckSlow(FrameState);

			TFrameState* InputFrameState = State.ReadFrame(State.GetLatestInputFrame());
			npCheckfSlow(InputFrameState, TEXT("Invalid InputFrame. %s"), *State.DebugString());

			TAuxState* AuxState = State.ReadAux(LastSerializedKeyframe);
			npCheckSlow(AuxState);
			
			FrameState->TotalSimTime.NetSerialize(P.Ar);	// 1. Time
			FrameState->SyncState.NetSerialize(P.Ar);		// 2. Sync
			InputFrameState->InputCmd.NetSerialize(P.Ar);	// 3. Input
			AuxState->NetSerialize(P.Ar); // 4. Aux

			// Server: send SimProxy and Interpolators. Whether this is in interpolation mode is really a client side thing (we want client to make this decision and transition between the two when necessary)
			State.CueDispatcher.NetSerializeSavedCues(Ar, ENetSimCueReplicationTarget::SimulatedProxy | ENetSimCueReplicationTarget::Interpolators); // 5. Cues
		}
		else
		{
			// ReconcileSimulationTime is where we will reconcile up to if we get an update from the server "in the past"
			// (But since we can get multiple NetSerializes before reconciling, don't let a stale update mess things up)
			if (State.GetTotalProcessedSimulationTime() > ReconcileSimulationTime)
			{
				ReconcileSimulationTime = State.GetTotalProcessedSimulationTime();
			}

			LastSerializedKeyframe = State.GetPendingTickFrame()+1;

			TFrameState* FrameState = State.WriteFrameDirect(LastSerializedKeyframe);
			npCheckSlow(FrameState);
			FrameState->TotalSimTime.NetSerialize(P.Ar); // 1. Time
			FrameState->SyncState.NetSerialize(P.Ar); // 2. Sync
			FrameState->InputCmd.NetSerialize(P.Ar); // 3. Input
			FrameState->SetFlag(ESimulationFrameStateFlags::InputWritten);

			TAuxState* AuxState = State.WriteAuxState(LastSerializedKeyframe);
			npCheckSlow(AuxState);
			AuxState->NetSerialize(P.Ar); // 4. Aux

			State.CueDispatcher.NetSerializeSavedCues(Ar, GetSimulatedUpdateMode() == ESimulatedUpdateMode::Interpolate ? ENetSimCueReplicationTarget::Interpolators : ENetSimCueReplicationTarget::SimulatedProxy); // 5. Cues
			
			State.ResetToFrame(LastSerializedKeyframe, true);
			if (State.GetTotalAllowedSimulationTime() < FrameState->TotalSimTime)
			{
				State.SetTotalAllowedSimulationTime(FrameState->TotalSimTime);
			}

			UE_NP_TRACE_NET_SERIALIZE_RECV(FrameState->TotalSimTime, LastSerializedKeyframe);

			UE_NP_TRACE_USER_STATE_INPUT(FrameState->InputCmd, LastSerializedKeyframe);
			UE_NP_TRACE_USER_STATE_SYNC(FrameState->SyncState, LastSerializedKeyframe);
			UE_NP_TRACE_USER_STATE_AUX(*AuxState, LastSerializedKeyframe);
			
			LastSerializedSimulationTime = FrameState->TotalSimTime;
			LastSerializedSyncState = FrameState->SyncState;
			LastSerializedInputCmd = FrameState->InputCmd;
			LastSerializedAuxState = *AuxState;
		}

		NETSIM_CHECKSUM(P.Ar);
	}

	// -------------------------------------------------------------------------------------------------
	// TRepController_Simulated::Reconcile
	// -------------------------------------------------------------------------------------------------
	void Reconcile(TSimulation* Simulation, TDriver* Driver, TNetworkedSimulationState<Model>& State)
	{
		if (ReconcileSimulationTime.IsPositive() == false)
		{
			return;
		}

		// The last serialized state is now committed. If we received multiple NetSerializes before a reconcile, only the last is committed.
		UE_NP_TRACE_NET_SERIALIZE_COMMIT();

		FNetworkSimTime TotalSimTime = State.GetTotalProcessedSimulationTime();
		check(TotalSimTime <= State.GetTotalAllowedSimulationTime());

		if (GetSimulatedUpdateMode() == ESimulatedUpdateMode::Extrapolate && NetworkSimulationModelCVars::EnableSimulatedReconcile())
		{
			// This is effectively a rollback: we went back in time during NetSerialize and are now going to catch up ReconcileSimulationTime ms.
			State.CueDispatcher.NotifyRollback(TotalSimTime);

			// Do we have time to make up? We may have extrapolated ahead of the server (totally fine - can happen with small amount of latency variance)
			FNetworkSimTime DeltaSimTime = ReconcileSimulationTime - TotalSimTime;
			if (DeltaSimTime.IsPositive() && NetworkSimulationModelCVars::EnableSimulatedReconcile())
			{
				SimulationExtrapolation(Simulation, Driver, State, DeltaSimTime);
			}
		}
		
		check(State.GetTotalProcessedSimulationTime() <= State.GetTotalAllowedSimulationTime());
		ReconcileSimulationTime.Reset();
	}

	// -------------------------------------------------------------------------------------------------
	// TRepController_Simulated::Tick
	// -------------------------------------------------------------------------------------------------
	void Tick(TDriver* Driver, TSimulation* Simulation, TNetworkedSimulationState<Model>& State, const FNetSimTickParameters& TickParameters)
	{
		// Tick if we are dependent simulation or extrapolation is enabled
		if (ParentSimulation || (GetSimulatedUpdateMode() == ESimulatedUpdateMode::Extrapolate))
		{
			// Don't start this simulation until you've gotten at least one update from the server
			if (State.GetTotalProcessedSimulationTime().IsPositive())
			{
				State.GiveSimulationTime(TickParameters.LocalDeltaTimeSeconds);
			}
			
			// Generate local input if we are ready to tick. Note that we pass false so we won't call into the Driver to produce the input, we will use the last serialized InputCmd's values
			TryGenerateLocalInput<Model>(Driver, State, false);
		}

		DoSimulationTicks(*Simulation, State, ESimulationTickContext::SimExtrapolate, State.GetLatestInputFrame());

		if (GetSimulatedUpdateMode() == ESimulatedUpdateMode::Interpolate)
		{
			const FNetworkSimTime::FRealTime InterpolationRealTime = Interpolator.template PostSimTick<TDriver>(Driver, State, TickParameters);
			const FNetworkSimTime InterpolationSimTime = FNetworkSimTime::FromRealTimeSeconds(InterpolationRealTime);

			State.CueDispatcher.SetMaxDispatchTime(InterpolationSimTime);
			State.CueDispatcher.SetConfirmedTime(InterpolationSimTime);
		}
		else
		{
			// Sync to latest frame if there is any
			const int32 PendingFrame = State.GetPendingTickFrame();
			if (PendingFrame >= 0)
			{
				Driver->FinalizeFrame(State.ReadFrame(PendingFrame)->SyncState, *State.ReadAux(PendingFrame));
			}

			State.CueDispatcher.ClearMaxDispatchTime();
			State.CueDispatcher.SetConfirmedTime(LastSerializedSimulationTime);
		}
	}
	
	// -------------------------------------------------------------------------------------------------
	// TRepController_Simulated::DependentRollbackBegin
	// -------------------------------------------------------------------------------------------------
	void DependentRollbackBegin(TSimulation* Simulation, TDriver* Driver, TNetworkedSimulationState<Model>& State, const FNetworkSimTime& RollbackDeltaTime, const int32 ParentFrame)
	{
		// FIXME: this isn't using RollbackDeltaTime
		TFrameState* RollbackState = State.WriteFrameDirect(LastSerializedKeyframe);
		RollbackState->SyncState = LastSerializedSyncState;
		RollbackState->InputCmd = LastSerializedInputCmd;
		RollbackState->TotalSimTime = LastSerializedSimulationTime;
		RollbackState->SetFlag(ESimulationFrameStateFlags::InputWritten);
		State.ResetToFrame(LastSerializedKeyframe, true);
		State.SetTotalAllowedSimulationTime(LastSerializedSimulationTime);

		State.CueDispatcher.NotifyRollback(LastSerializedSimulationTime);
	}
		
	void DependentRollbackStep(TSimulation* Simulation, TDriver* Driver, TNetworkedSimulationState<Model>& State, const FNetworkSimTime& StepTime, const int32 ParentFrame, const bool bFinalStep)
	{
		State.SetTotalAllowedSimulationTime( State.GetTotalAllowedSimulationTime() + StepTime );
		SimulationExtrapolation(Simulation, Driver, State, StepTime);
	}

private:
	
	void SimulationExtrapolation(TSimulation* Simulation, TDriver* Driver, TNetworkedSimulationState<Model>& State, const FNetworkSimTime DeltaSimTime)
	{
		// We have extrapolated ahead of the server. The latest network update is now "in the past" from what we rendered last frame.
		// We will insert a new frame to make up the difference from the last known state to where we want to be in the now.

		const int32 InputFrame = State.GetPendingTickFrame();
		const int32 OutputFrame = InputFrame + 1;

		// Override the FrameDeltaTime
		TFrameState* InputFrameState = State.ReadFrame(InputFrame);
		npCheckSlow(InputFrameState);
		npEnsureSlow(InputFrameState->HasFlag(ESimulationFrameStateFlags::InputWritten));
		InputFrameState->FrameDeltaTime.Set(DeltaSimTime);

		// Do the actual update
		TSimulationDoTickFunc<Model>::DoTick(*Simulation, State, ESimulationTickContext::ResimExtrapolate);
	}

	int32 LastSerializedKeyframe;
	FNetworkSimTime ReconcileSimulationTime;

	// Temp: this should go away
	FNetworkSimTime LastSerializedSimulationTime;
	TInputCmd LastSerializedInputCmd;
	TSyncState LastSerializedSyncState;
	TAuxState LastSerializedAuxState;
};
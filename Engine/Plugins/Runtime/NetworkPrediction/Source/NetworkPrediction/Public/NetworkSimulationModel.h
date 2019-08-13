// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkPredictionTypes.h"
#include "NetworkSimulationModelBuffer.h"
#include "NetworkSimulationModelTypes.h"

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	CVars and compile time constants
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace NetworkSimulationModelCVars
{
static int32 EnableLocalPrediction = 1;
static FAutoConsoleVariableRef CVarEnableLocalPrediction(TEXT("ns.EnableLocalPrediction"),
	EnableLocalPrediction, TEXT("Toggle local prediction. Meant for debugging."), ECVF_Default);

static int32 ForceReconcile = 0;
static FAutoConsoleVariableRef CVarForceReconcile(TEXT("ns.ForceReconcile"),
	ForceReconcile, TEXT("Forces reconcile even if state does not differ. E.g, force resimulation after every netupdate."), ECVF_Default);

static int32 MaxInputCmdsFrame = 1;
static FAutoConsoleVariableRef CVarMaxInputCmdsFrame(TEXT("ns.MaxInputCmdsFrame"),
	MaxInputCmdsFrame, TEXT("Max cap on how many input cmds can be processed in a frame."), ECVF_Default);
}

#ifndef NETSIM_NETCONSTANT_NUM_BITS_KEYFRAME
	#define NETSIM_NETCONSTANT_NUM_BITS_KEYFRAME 8	// Allows you to override this setting via UBT, but access via cleaner FActorMotionNetworkingConstants::NUM_BITS_KEYFRAME
#endif

struct FNetworkSimulationSerialization
{
	// How many bits we use to encode the key frame number for buffers.
	// Client Keyframes are stored locally as 32 bit integers, but we use a smaller # of bits to NetSerialize.
	// Frames are only relatively relevant: the absolute value doesn't really matter. We just need to detect newer/older.
	enum { NUM_BITS_KEYFRAME = NETSIM_NETCONSTANT_NUM_BITS_KEYFRAME };		

	// Abs max value we encode into the bit writer
	enum { MAX_KEYFRAME_WRITE = 1 << NUM_BITS_KEYFRAME };

	// This is the threshold at which we would wrap around and incorrectly assign a keyframe on the receiving side.
	// E.g, If there are KEYFRAME_ERROR_THRESHOLD keyframes that do not make it across from sender->receiver, the
	// receiver will have incorrect local values. With 8 bits, this works out to be 128 frames or about 2 seconds at 60fps.
	enum { KEYFRAME_ERROR_THRESHOLD = MAX_KEYFRAME_WRITE / 2};

	// Helper to serialize the int32 HeadKeyframe. Returns the unpacked value (this will be same as input in the save path)
	static int32 SerializeKeyframe(FArchive& Ar, int32 LocalHeadKeyframe)
	{
		if (Ar.IsSaving())
		{
			((FNetBitWriter&)Ar).WriteIntWrapped( LocalHeadKeyframe, MAX_KEYFRAME_WRITE );
			return LocalHeadKeyframe;
		}
		
		return MakeRelative(((FNetBitReader&)Ar).ReadInt( MAX_KEYFRAME_WRITE ), LocalHeadKeyframe, MAX_KEYFRAME_WRITE );
	}
};


// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	Templated Replicators/Reconciliars
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

/** Replicates only the latest element. Does not synchronize keyframe */
template<typename TBufferTypes, ENetworkSimBufferTypeId BufferId>
struct TReplicator_LatestOnly
{
	using TState = typename TBufferTypes::template select_type<BufferId>::type;

	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{ 
		return Buffers.template Get<BufferId>().GetDirtyCount(); 
	}

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickInfo<TBufferTypes>& TickInfo)
	{
		FArchive& Ar = P.Ar;

		auto& Buffer = Buffers.template Get<BufferId>();
		TState* State = nullptr;
		
		if (Ar.IsSaving())
		{
			State = Buffer.GetElementFromHead(0);
			check(State); // We should not be here if the buffer is empty. Want to avoid serializing an "empty" flag at the top here.
		}
		else
		{
			State = Buffer.GetWriteNext();
		}

		State->NetSerialize(Ar);
	}
};

template<typename TBufferTypes>
struct TReplicator_SimulatedExtrapolatedReconciliar
{
	using TSyncState = typename TBufferTypes::TSyncState;
	
	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{
		return Buffers.Sync.GetDirtyCount(); 
	}

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickInfo<TBufferTypes>& TickInfo)
	{
		FArchive& Ar = P.Ar;
		TSyncState* State = nullptr;
		if (Ar.IsSaving())
		{
			State = Buffers.Sync.GetElementFromHead(0);
			check(State); // We should not be here if the buffer is empty. Want to avoid serializing an "empty" flag at the top here.
			SerializedTimeKeeper = TickInfo.ProcessedSimulationTime;
		}
		else
		{
			State = Buffers.Sync.GetWriteNext();
			bPendingReconciliation = true;
		}

		SerializedTimeKeeper.NetSerialize(Ar);
		State->NetSerialize(Ar);
	}

	template<typename T, typename TDriver>
	void Reconcile(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickInfo<TBufferTypes>& TickInfo)
	{
		if (!bPendingReconciliation)
			return;

		bPendingReconciliation = false;

		// This is still being worked out. We are almost there but not quite.
		// Processing input commands is pretty rigid right now. Since we just added a new SyncState keyframe in ::NetSerialize,
		// we need to insert a fake input command so the extrapolation code in the NetSimModel can continue to work.
		// This can be improved.

		{
			// Generate a new, fake, command since we just added a new sync state to head (via GetWriteNext)
			TickInfo.GetNextInputForWrite(Buffers);
			// Gotta fake this too
			TickInfo.LastLocalInputGFrameNumber--; 
			// Set our LastProcessedInputKeyframe to fake that we handled it
			TickInfo.LastProcessedInputKeyframe = Buffers.Sync.GetHeadKeyframe();
		}
	}

private:
	
	TSimulationTimeKeeper<TBufferTypes> SerializedTimeKeeper;
	bool bPendingReconciliation = false;
};

/** Replicates the latest sequence of N elements. N is dynamic, not compiled in. */
template<typename TBufferTypes, ENetworkSimBufferTypeId BufferId>
struct TReplicator_DynamicSequence
{
	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const 
	{
		return Buffers.template Get<BufferId>().GetDirtyCount(); 
	}
	
	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickInfo<TBufferTypes>& TickInfo)
	{
		auto& Buffer = Buffers.template Get<BufferId>();
		FArchive& Ar = P.Ar;
		uint8 SerializedNumElements = FMath::Min<uint8>(MaxNumElements, Buffer.GetNumValidElements());
		Ar << SerializedNumElements;

		const int32 HeadKeyframe = FNetworkSimulationSerialization::SerializeKeyframe(Ar, Buffer.GetHeadKeyframe());
		const int32 StartingKeyframe = FMath::Max(0, HeadKeyframe - SerializedNumElements + 1);

		if (Ar.IsLoading())
		{
			const int32 PrevHead = Buffer.GetHeadKeyframe();
			if (PrevHead < StartingKeyframe && PrevHead >= 0)
			{
				// There is a gap in the stream. In some cases, we want this to be a "fault" and bubble up. We may want to synthesize state or maybe we just skip ahead.
				UE_LOG(LogNetworkSim, Warning, TEXT("Fault: gap in received buffer. PrevHead: %d. Received: %d-%d. Reseting previous buffer contents"), PrevHead, StartingKeyframe, HeadKeyframe);
			}

			Buffer.ResetNextHeadKeyframe(StartingKeyframe);
		}

		for (int32 Keyframe = StartingKeyframe; Keyframe <= HeadKeyframe; ++Keyframe)
		{
			// This, as is, is bad. The intention is that these functions serialize multiple items in some delta compressed fashion.
			// As is, we are just serializing the elements individually.
			auto* Cmd = Ar.IsLoading() ? Buffer.GetWriteNext() : Buffer.FindElementByKeyframe(Keyframe);
			Cmd->NetSerialize(P);
		}
	}

private:
	
	uint8 MaxNumElements = 3;
};

/** Replicates TSyncState and does basic reconciliation. */
template<typename TBufferTypes>
struct TReplicator_BasicReconciliar
{
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;

	int32 GetLastSerializedKeyframe() const { return LastSerializedKeyframe; }
	bool IsReconcileFaultDetected() const { return bReconcileFaultDetected; }
	const TSimulationTimeKeeper<TBufferTypes>& GetLastSerializedSimulationTimeKeeper() const { return SerializedTimeKeeper; }

	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{
		return Buffers.Sync.GetDirtyCount();
	}

	// --------------------------------------------------------------------
	//	NetSerialize
	// --------------------------------------------------------------------
	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickInfo<TBufferTypes>& TickInfo)
	{
		FArchive& Ar = P.Ar;

		const int32 SerializedHeadKeyframe = FNetworkSimulationSerialization::SerializeKeyframe(Ar, Buffers.Sync.GetHeadKeyframe());
		TSyncState* SerializedState = nullptr;

		// Serialize total simulation time. This isn't really necessary since we have the keyframe above. 
		SerializedTimeKeeper = TickInfo.ProcessedSimulationTime;
		SerializedTimeKeeper.NetSerialize(P);

		if (Ar.IsSaving())
		{
			// Server serialize the latest state
			SerializedState = Buffers.Sync.GetElementFromHead(0);
		}
		else
		{
			// Its possible we process two packets in between ticks. This isn't really a problem but should be rare.
			if (bPendingReconciliation)
			{
				UE_LOG(LogNetworkSim, Warning, TEXT("bPendingReconciliation while in ::NetSerialize. LastSerializedKeyframe: %d. New SerializedHeadKeyframe: %d."), LastSerializedKeyframe, SerializedHeadKeyframe);
			}

			// Lazy init the reconciliation buffer. We don't need this on the server/writing side. (Fixme, with templated approach we could easily specialize this to an inline element instead of buffer?)
			if (ReconciliationBuffer.GetMaxNumElements() == 0)
			{
				ReconciliationBuffer.SetBufferSize(1);
			}

			// Reset the head to match the server. This is not critical, we just need a place to store this, but it makes the code in the reconciliation a bit nicer since the reconcilated frame number = head frame.
			ReconciliationBuffer.ResetNextHeadKeyframe(SerializedHeadKeyframe);
			SerializedState = ReconciliationBuffer.GetWriteNext();
		}

		SerializedState->NetSerialize(Ar);

		if (Ar.IsLoading())
		{
			bReconcileFaultDetected = false;
			bPendingReconciliation = false;

			// The state the client predicted that corresponds to the state the server just serialized to us
			if (TSyncState* ClientExistingState = Buffers.Sync.FindElementByKeyframe(SerializedHeadKeyframe))
			{
				if (ClientExistingState->ShouldReconcile(*SerializedState) || (NetworkSimulationModelCVars::ForceReconcile > 0))
				{
					UE_CLOG(!Buffers.Input.IsValidKeyframe(SerializedHeadKeyframe), LogNetworkSim, Error, TEXT("::NetSerialize: Client InputBuffer does not contain data for frame %d. {%s} {%s}"), SerializedHeadKeyframe, *Buffers.Input.GetBasicDebugStr(), *Buffers.Sync.GetBasicDebugStr());
					bPendingReconciliation =  true;
				}
			}
			else
			{
				// We don't have corresponding local state. There are two cases:
				if (NetworkSimulationModelCVars::EnableLocalPrediction) // Fixme: this is awkward. Expected if player prediction is disabled but coupling like this feels bad.
				{
					UE_LOG(LogNetworkSim, Warning, TEXT("::NetSerialize Fault: SyncBuffer does not contain data for frame %d. [%d-%d]"), SerializedHeadKeyframe, Buffers.Sync.GetTailKeyframe(), Buffers.Sync.GetHeadKeyframe());
				}

				// Case 1: the serialized state is older than what we've kept in our buffer. A bigger buffer would solve this! (at the price of more resimulated frames to recover when this happens)
				if (SerializedHeadKeyframe < Buffers.Sync.GetTailKeyframe())
				{
					// This is a reconcile fault and we just need to chill. We'll stop sending user commands until the cmds in flight flush through the system and we catch back up.
					bReconcileFaultDetected = true;
				}
				else
				{
					// Case 2: We've received a newer frame than what we've processed locally. This could happen if we are buffering our inputs locally (but still sending to the server).
					// Since this doesn't require resimulating, we can just set the authoritative state here and continue on. Ticking logic will need to detect this gap and skip
					// our LastProcessedInputKeyframe ahead.

					// However, this keyframe better be a valid input. Otherwise how did the server generate it?
					checkf(SerializedHeadKeyframe <= Buffers.Input.GetHeadKeyframe(), TEXT("Received newer motionstate that doesn't correspond to valid input cmd. SerializedHeadKeyframe: %d. {%s} {%s}"),
						SerializedHeadKeyframe, *Buffers.Input.GetBasicDebugStr(), *Buffers.Sync.GetBasicDebugStr());

					Buffers.Sync.ResetNextHeadKeyframe(SerializedHeadKeyframe);
					TSyncState* ClientMotionState = Buffers.Sync.GetWriteNext();
					*ClientMotionState = *SerializedState;
				}
			}
		}

		LastSerializedKeyframe = SerializedHeadKeyframe;
	}

	// --------------------------------------------------------------------
	//	Reconcile
	// --------------------------------------------------------------------
	template<typename T, typename TDriver>
	void Reconcile(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickInfo<TBufferTypes>& TickInfo)
	{
		if (bPendingReconciliation == false)
		{
			return;
		}
		bPendingReconciliation = false;
		check(ReconciliationBuffer.GetNumValidElements() == 1); // this function assumes only 1 element

		const int32 ReconciliationKeyframe = ReconciliationBuffer.GetHeadKeyframe();
		TSyncState* ServerState = ReconciliationBuffer.GetElementFromHead(0);

		TInputCmd* ClientInputCmd = Buffers.Input.FindElementByKeyframe( ReconciliationKeyframe );
		if (ClientInputCmd == nullptr)
		{
			// Fault: no historic command for this frame to reconcile with.
			UE_LOG(LogNetworkSim, Error, TEXT("Client InputBuffer does not contain data for frame %d. {%s} {%s}"), ReconciliationKeyframe, *Buffers.Input.GetBasicDebugStr(), *Buffers.Sync.GetBasicDebugStr());
			return;
		}

		TSyncState* ClientSyncState = Buffers.Sync.FindElementByKeyframe( ReconciliationKeyframe );

		// Should not be here if the client doesn't have a valid state. See ::NetSerialize
		checkf(ClientSyncState != nullptr, TEXT("SyncBuffer does not contain data for frame %d. %s"), ReconciliationKeyframe, *Buffers.Sync.GetBasicDebugStr());

		// -------------------------------------------------------------------------------------------------------------------------
		// Resimulate
		// -------------------------------------------------------------------------------------------------------------------------

		ServerState->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::LastConfirmed, ReconciliationKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);
		ClientSyncState->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::FirstMispredicted, ReconciliationKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);

		TSyncState* LatestClient = Buffers.Sync.GetElementFromHead(0);

		// Copy authoritative state over client state (FIXME: we may want to store this off historically somewhere? Or will VLog be enough for debugging?)
		*ClientSyncState = *ServerState;

		// Set the canonical simulation time to what we received (we will advance it as we resimulate)
		TickInfo.ProcessedSimulationTime = SerializedTimeKeeper;
		TickInfo.LastProcessedInputKeyframe = ReconciliationKeyframe;
		
		// Resimulate up to our latest SyncBuffer frame. Note that we may have unprocessed user commands in the command buffer at this point.
		const int32 LatestKeyframe = Buffers.Sync.GetHeadKeyframe();		
		for (int32 Keyframe = ReconciliationKeyframe+1; Keyframe <= LatestKeyframe; ++Keyframe)
		{
			// Keyframe is the frame we are resimulating right now.
			TInputCmd* ResimulateCmd	= Buffers.Input.FindElementByKeyframe(Keyframe);
			TSyncState* PrevMotionState = Buffers.Sync.FindElementByKeyframe(Keyframe - 1);
			TSyncState* NextMotionState = Buffers.Sync.FindElementByKeyframe(Keyframe);
			
			check(ResimulateCmd);
			check(PrevMotionState);
			check(NextMotionState);

			// TEMP (Aux buffer not fully plumbed through the system yet)
			// ------------------------------------------------------
			TAuxState TempAuxState;
			TAuxState* AuxState = Buffers.Aux.FindElementByKeyframe(Keyframe);
			if (AuxState == nullptr)
			{
				AuxState = &TempAuxState;
			}
			// ------------------------------------------------------

			// Log out the Mispredicted state that we are about to overwrite.
			NextMotionState->VisualLog( FVisualLoggingParameters(Keyframe == LatestKeyframe ? EVisualLoggingContext::LastMispredicted : EVisualLoggingContext::OtherMispredicted, Keyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);

			// Do the actual update
			T::Update(Driver, *ResimulateCmd, *PrevMotionState, *NextMotionState, *AuxState);
			
			// Update TickInfo
			TickInfo.ProcessedSimulationTime.AccumulateTimeFromInputCmd(*ResimulateCmd);
			TickInfo.LastProcessedInputKeyframe = Keyframe;

			// Log out the newly predicted state that we got.
			NextMotionState->VisualLog( FVisualLoggingParameters(Keyframe == LatestKeyframe ? EVisualLoggingContext::LastPredicted : EVisualLoggingContext::OtherPredicted, Keyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);
		}
	}

private:
	
	TReplicationBuffer<TSyncState> ReconciliationBuffer;
	
	TSimulationTimeKeeper<TBufferTypes> SerializedTimeKeeper; // last serialized time keeper

	int32 LastSerializedKeyframe = -1;
	bool bPendingReconciliation = false;	// Reconciliation is pending: we need to reconcile state from the server that differs from the locally predicted state
	bool bReconcileFaultDetected = false;	// A fault was detected: we received state from the server that we are unable to reconcile with locally predicted state
};

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	TNetworkedSimulationModel
//	
//	* Has all logic for "ticking, advancing buffers, calling Update, calling ServerRPC etc
//	* Doesn't have anything about update component, movesweep, etc
//	* Concept of "IDriver" which is the owning object that is driving the network sim. This is the interface to the outside UE4 world.
//	* Has 4 buffers:
//		-Input: Generated by a client / not the authority.
//		-Sync: What we are trying to keep in sync. The state that evolves frame to frame with an Update function.
//		-Aux: State that is also an input into the simulation but does not intrinsically evolve from to frame. Changes to this state can be trapped/tracked/predicted.
//		-Debug: Replicated buffer from server->client with server-frame centered debug information. Compiled out of shipping builds.
//
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

template <
	typename T,
	typename TBufferTypes,
	typename TRepProxyServerRPC = TReplicator_DynamicSequence<TBufferTypes, ENetworkSimBufferTypeId::Input>,
	typename TRepProxyAutonomous = TReplicator_BasicReconciliar<TBufferTypes>,
	typename TRepProxySimulated = TReplicator_SimulatedExtrapolatedReconciliar<TBufferTypes>,
	typename TRepProxyReplay = TReplicator_DynamicSequence<TBufferTypes, ENetworkSimBufferTypeId::Sync>,
	typename TRepProxyDebug = TReplicator_DynamicSequence<TBufferTypes, ENetworkSimBufferTypeId::Debug>
>
class TNetworkedSimulationModel : public IReplicationProxy
{
public:

	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;
	using TDebugState = typename TBufferTypes::TDebugState;

	class IDriver
	{
	public:
		virtual void InitSyncState(TSyncState& OutSyncState) const = 0;	// Called to create initial value of the sync state.
		virtual void FinalizeFrame(const TSyncState& SyncState) = 0; // Called from the Network Sim at the end of the sim frame when there is new sync data.
	};

	struct FTickParameters
	{
		ENetRole Role;
		float LocalDeltaTimeSeconds;
	};

	template<typename TDriver>
	void Tick(TDriver* Driver, const FTickParameters& Parameters)
	{
		TDebugState* const DebugState = GetNextDebugStateWrite();
		if (DebugState)
		{
			DebugState->LocalDeltaTimeSeconds = Parameters.LocalDeltaTimeSeconds;
			DebugState->LocalGFrameNumber = GFrameNumber;
			DebugState->ProcessedKeyframes.Reset();
		}

		// How many commands we are allowed to process right now. This will need to be built out a bit more to handle shifting network conditions etc. For now, just a cvar.
		int32 AllowCmds = NetworkSimulationModelCVars::MaxInputCmdsFrame;

		switch (Parameters.Role) {

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		//														Reconciliation
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		case ROLE_AutonomousProxy:
		{
			// Client: don't allow buffering and ensure continuous stream of inputs. This will need to be built out a bit more. The main points are:
			// -Don't buffer input locally (unless we explicitly opt into it as a lag hiding measurement). Don't want to accidentally introduce application latency.
			// -Don't send server input cmds that we didn't actually process. Current Input buffer replication is rather hard coded to send continuous frames and doesn't handle gaps.

			// Recovering from this is possible but messy. We could immediately set things up so that we process the latest user cmd.
			// The tricky part is making sure we the server does the same thing. We would need to set the tail of the input cmd stream so
			// that we don't replicate any cmds that were skipped. Then server side it would have to detect this and know to jump ahead.
			// At this point in development, the complexity this adds to the overall system is not worth the benefit. The better way to
			// fix this for now is to closely guard what we allow into the client's InputCmdBuffer

			// You can be at head (no new cmds to process) or right before the head (1 new cmd to process)
			checkf(Buffers.Input.GetHeadKeyframe() - TickInfo.LastProcessedInputKeyframe <= 1, TEXT("Client Input Processing is out of sync. LastProcessedInputKeyframe: %d. InputBuffer: [%d - %d]"), 
				TickInfo.LastProcessedInputKeyframe, Buffers.Input.GetTailKeyframe(), Buffers.Input.GetHeadKeyframe());

			// We should be processing input from this GFrameNumber. If we aren't, then our DeltaTime is probably off (input cmd's delta time wont match the actual delta time of the frame we sim on which will cause artifacts).
			// We also want to avoid accidentally introducing input latency as a matter of principle (input latency bad)
			// We *can* support local input buffering in two ways (neither currently implemented):
			//	-Fixed time steps (delta times will be constant so buffering wont matter)
			//	-Variable time steps with "interpolation layer": we will need to be able to absorb fluctuations in frame rate so there will be render frames that doesn't process any new input and
			//		render frames that have to process multiple inputs. (E.g, the goal would be to always "see" an update every render frame, even if we can't actually tick a new sim frame every render frame. Interpolation would be needed to accomplish this)
			// If either of the above ideas are implemented, it will be done as an optional/templated parameter to the network sim class.
			checkf(TickInfo.LastLocalInputGFrameNumber == 0 || GFrameNumber == TickInfo.LastLocalInputGFrameNumber, TEXT("TNetworkedSimulationModel running on stale input. You may have an ordering issue between TNetworkedSimulationModel::GetNextInputForWrite and TNetworkedSimulationModel::Tick. "
				"GFrameNumber: %d LastLocalInputGFrameNumber: %d"), GFrameNumber, TickInfo.LastLocalInputGFrameNumber);

		
			// Note that is is important for the NetworkSimModel to control *when* the reconcile happens. The replicator objects themselves should never do
			// reconciliation inside a NetSerialze call. Tick may not be the permanent place for this, but it can't be in NetSerialize.
			RepProxy_Autonomous.template Reconcile<T, TDriver>(Driver, Buffers, TickInfo);

			// Don't process new user commands if we are dealing with a reconciliation fault. That means we weren't able to correlate our predicted position with what the
			// server last sent us. If we are hitting this, it means we are waiting for the in flight commands to flush through the system. The proxy rep object will handle recovering.
			if (RepProxy_Autonomous.IsReconcileFaultDetected())
			{
				AllowCmds = 0;
			}

			// Check SyncBuffer being ahead of processed Keyframes. This would happen in cases where we are either not predicting or are buffering our input locally
			// while sending latest cmds to the server. Essentially, we got the authoritative motion state from the server before we ran the simulation locally.
			if (Buffers.Sync.GetHeadKeyframe() > TickInfo.LastProcessedInputKeyframe)
			{
				checkf(Buffers.Input.IsValidKeyframe(Buffers.Sync.GetHeadKeyframe()), TEXT("MotionState and InputCmd buffers are out of system. LastProcessedInputKeyframe: %d {%s} vs {%s}"),
					TickInfo.LastProcessedInputKeyframe, *Buffers.Sync.GetBasicDebugStr(), *Buffers.Input.GetBasicDebugStr());

				UE_LOG(LogNetworkSim, Warning, TEXT("Skipping local input frames because we have newer data in SyncBuffer. LastProcessedInputKeyframe: %d. {%s} {%s}"),
					TickInfo.LastProcessedInputKeyframe, *Buffers.Sync.GetBasicDebugStr(), *Buffers.Input.GetBasicDebugStr());

				TickInfo.LastProcessedInputKeyframe = Buffers.Sync.GetHeadKeyframe();
			}

			if (NetworkSimulationModelCVars::EnableLocalPrediction == 0)
			{
				AllowCmds = 0; // Don't process any commands this frame
				TickInfo.LastProcessedInputKeyframe = Buffers.Input.GetHeadKeyframe(); // Increment so we can accept a new command next frame
			}

			break;
		}

		case ROLE_Authority:
		{
			if ( TickInfo.LastProcessedInputKeyframe+1 < Buffers.Input.GetTailKeyframe() )
			{
				// We've missed commands
				UE_LOG(LogNetworkSim, Warning, TEXT("::Tick missing inputcmds. LastProcessedInputKeyframe: %d. %s"), TickInfo.LastProcessedInputKeyframe, *Buffers.Input.GetBasicDebugStr());
				TickInfo.LastProcessedInputKeyframe = Buffers.Input.GetTailKeyframe()+1;
			}
			break;
		}

		case ROLE_SimulatedProxy:
		{
			RepProxy_Simulated.template Reconcile<T, TDriver>(Driver, Buffers, TickInfo);

			// Extrapolation: this still needs work  (needs to be a setting/state somewhere and we need to better handle the relationship with NetSerialize and Reconcile)
			if (TInputCmd* SynthesizedCmd = GetNextInputForWrite(Parameters.LocalDeltaTimeSeconds))
			{

			}
			break;
		}

		} // end switch

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		//														Input Processing
		// -------------------------------------------------------------------------------------------------------------------------------------------------

		int32 NumProcessed = 0;
		while (AllowCmds-- > 0)
		{
			const int32 Keyframe = TickInfo.LastProcessedInputKeyframe+1;
			if (TInputCmd* NextCmd = Buffers.Input.FindElementByKeyframe(Keyframe))
			{
				// The SyncedState buffer needs to be in sync here:
				//	-We want it to have a SyncedState, but it may not on the first frame through (thats ok).
				//  -Its HeadKeyframe should be one behind the Keyframe we are about to process.
				//
				// Note, InputCmds start @ Keyframe=1. The first SyncedState that Update produces will go in KeyFrame=1.
				// (E.g, InputCmd @ keyframe=X is used to generate MotionState @ keyframe=X)
				// This means that SyncedState @ keyframe=0 is always created here via InitSyncState.
				// This also means that we never actually process InputCmd @ keyframe=0. Which is why LastProcessedInputKeyframe is initialized to 0 ("already processed")
				// and the buffer has an empty element inserted in InitLocalInputBuffer.

				if (Buffers.Sync.GetHeadKeyframe() != TickInfo.LastProcessedInputKeyframe)
				{
					if (TickInfo.LastProcessedInputKeyframe != 0)
					{
						// This shouldn't happen, but is not fatal. We are reseting the sync state buffer.
						UE_LOG(LogNetworkSim, Warning, TEXT("Break in SyncState continuity. LastProcessedInputKeyframe: %d. SyncBuffer.GetHeadKeyframe(): %d. Role=%d"), TickInfo.LastProcessedInputKeyframe, Buffers.Sync.GetHeadKeyframe(), (int32)Parameters.Role);
					}

					// We need an initial/current state. Get this from the sim driver
					Buffers.Sync.ResetNextHeadKeyframe(TickInfo.LastProcessedInputKeyframe);
					TSyncState* StartingState = Buffers.Sync.GetWriteNext();
					Driver->InitSyncState(*StartingState);
					
				}

				TSyncState* PrevSyncState = Buffers.Sync.FindElementByKeyframe(TickInfo.LastProcessedInputKeyframe);
				TSyncState* NextSyncState = Buffers.Sync.GetWriteNext();

				check(PrevSyncState != nullptr);
				check(NextSyncState != nullptr);
				check(Buffers.Sync.GetHeadKeyframe() == Keyframe);
				
				if (DebugState)
				{
					DebugState->ProcessedKeyframes.Add(Keyframe);
				}

				TAuxState AuxState; // Temp: aux buffer not implemented yet

				T::Update(Driver, *NextCmd, *PrevSyncState, *NextSyncState, AuxState);
				TickInfo.ProcessedSimulationTime.AccumulateTimeFromInputCmd(*NextCmd);
				NumProcessed++;

				TickInfo.LastProcessedInputKeyframe = Keyframe;
			}
			else
			{
				// Not a warning in itself anymore. Though the concept of "haven't processed cmds in x MS" is something we want to define.
				break;
			}
		}

		// FIXME: this needs to be sorted out. We really want to check if there is new sync state and then call this here.
		// Call into the driver to sync to the latest state if we processed any input
		//if (NumProcessed > 0)
		{
			//check(SyncBuffer.GetNumValidElements() > 0);
			if (Buffers.Sync.GetNumValidElements() > 0)
			{
				Driver->FinalizeFrame(*Buffers.Sync.GetElementFromHead(0));
			}
		}

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		//														Debug
		// -------------------------------------------------------------------------------------------------------------------------------------------------

		// Finish debug state buffer recording (what the server processed each frame)
		if (DebugState)
		{
			DebugState->LastProcessedKeyframe = TickInfo.LastProcessedInputKeyframe;
			DebugState->HeadKeyframe = Buffers.Input.GetHeadKeyframe();
		}

		// Historical data recording (longer buffers for historical reference)
		if (auto* HistoricData = GetHistoricBuffers())
		{
			HistoricData->Input.CopyAndMerge(Buffers.Input);
			HistoricData->Sync.CopyAndMerge(Buffers.Sync);
			HistoricData->Aux.CopyAndMerge(Buffers.Aux);
		}
	}	
	
	void InitializeForNetworkRole(const ENetRole Role, const bool IsLocallyControlled, const FNetworkSimulationModelInitParameters& Parameters)
	{
		Buffers.Input.SetBufferSize(Parameters.InputBufferSize);
		Buffers.Sync.SetBufferSize(Parameters.SyncedBufferSize);
		Buffers.Aux.SetBufferSize(Parameters.AuxBufferSize);

		if (GetDebugBuffer())
		{
			GetDebugBuffer()->SetBufferSize(Parameters.DebugBufferSize);
		}

		if (auto* MyHistoricBuffers = GetHistoricBuffers(true))
		{
			MyHistoricBuffers->Input.SetBufferSize(Parameters.HistoricBufferSize);
			MyHistoricBuffers->Sync.SetBufferSize(Parameters.HistoricBufferSize);
			MyHistoricBuffers->Aux.SetBufferSize(Parameters.HistoricBufferSize);
		}

		if (IsLocallyControlled)
		{
			check(Parameters.InputBufferSize > 0); // If you tell me this is locally controlled, you need to have an input buffer.
			InitLocalInputBuffer();
		}
	}

	void NetSerializeProxy(EReplicationProxyTarget Target, const FNetSerializeParams& Params)
	{
		// You are not allowed to change the simulations TickInfo while NetSerializing. Since all rep proxies are templated
		// and there is not interface/base class, we are enforcing the constness at the call site.
		const TSimulationTickInfo<TBufferTypes>& ConstTickInfo = const_cast<const TSimulationTickInfo<TBufferTypes>&>(TickInfo);

		switch(Target)
		{
		case EReplicationProxyTarget::ServerRPC:
			RepProxy_ServerRPC.NetSerialize(Params, Buffers, ConstTickInfo);
			break;
		case EReplicationProxyTarget::AutonomousProxy:
			RepProxy_Autonomous.NetSerialize(Params, Buffers, ConstTickInfo);
			break;
		case EReplicationProxyTarget::SimulatedProxy:
			RepProxy_Simulated.NetSerialize(Params, Buffers, ConstTickInfo);
			break;
		case EReplicationProxyTarget::Replay:
			RepProxy_Replay.NetSerialize(Params, Buffers, ConstTickInfo);
			break;
		case EReplicationProxyTarget::Debug:
#if NETSIM_MODEL_DEBUG
			RepProxy_Debug.NetSerialize(Params, Buffers, ConstTickInfo);
			break;
#endif
		default:
			checkf(false, TEXT("Unknown: %d"), (int32)Target);
		};
	}

	int32 GetProxyDirtyCount(EReplicationProxyTarget Target)
	{
		switch(Target)
		{
		case EReplicationProxyTarget::ServerRPC:
			return RepProxy_ServerRPC.GetProxyDirtyCount(Buffers);
		case EReplicationProxyTarget::AutonomousProxy:
			return RepProxy_Autonomous.GetProxyDirtyCount(Buffers);
		case EReplicationProxyTarget::SimulatedProxy:
			return RepProxy_Simulated.GetProxyDirtyCount(Buffers);
		case EReplicationProxyTarget::Replay:
			return RepProxy_Replay.GetProxyDirtyCount(Buffers);
		case EReplicationProxyTarget::Debug:
#if NETSIM_MODEL_DEBUG
			return RepProxy_Debug.GetProxyDirtyCount(Buffers);
#endif
		default:
			checkf(false, TEXT("Unknown: %d"), (int32)Target);
			return 0;
		};
	}
	
	TInputCmd* GetNextInputForWrite(float DeltaTime)
	{
		if (TInputCmd* Next = TickInfo.GetNextInputForWrite(Buffers))
		{
			Next->FrameDeltaTime = DeltaTime;
			return Next;
		}
		return nullptr;
	}

	void InitLocalInputBuffer()
	{
		// Buffer should also be empty before calling this
		check(Buffers.Input.GetHeadKeyframe() == INDEX_NONE);

		// We want to start with an empty command in the input buffer. See notes in input buffer processing function.
		*Buffers.Input.GetWriteNext() = TInputCmd();
		TickInfo.LastLocalInputGFrameNumber = 0;
	}	
	
	TSimulationTickInfo<TBufferTypes> TickInfo;	// Manages simulation time and what inputs we are processed

	TNetworkSimBufferContainer<TBufferTypes> Buffers;

	TRepProxyServerRPC RepProxy_ServerRPC;
	TRepProxyAutonomous RepProxy_Autonomous;
	TRepProxySimulated RepProxy_Simulated;
	TRepProxyReplay RepProxy_Replay;

	// ------------------------------------------------------------------
	// RPC Sending helper: provides basic send frequency settings for tracking when the Server RPC can be invoked.
	// Note that the Driver is the one that must call the RPC, that cannot be rolled into this templated structure.
	// More flexbile/dynamic send rates may be desireable. There is not reason this *has* to be done here, it could
	// completely be tracked at the driver level, but that will also push more boilerplate to that layer for users.
	// ------------------------------------------------------------------

	void SetDesiredServerRPCSendFrequency(float DesiredHz) { ServerRPCThresholdTimeSeconds = 1.f / DesiredHz; }
	bool ShouldSendServerRPC(ENetRole OwnerRole, float DeltaTimeSeconds)
	{
		// Don't allow a large delta time to pollute the accumulator
		const float CappedDeltaTimeSeconds = FMath::Min<float>(DeltaTimeSeconds, ServerRPCThresholdTimeSeconds);
		if (OwnerRole == ROLE_AutonomousProxy)
		{
			ServerRPCAccumulatedTimeSeconds += DeltaTimeSeconds;
			if (ServerRPCAccumulatedTimeSeconds >= ServerRPCThresholdTimeSeconds)
			{
				ServerRPCAccumulatedTimeSeconds -= ServerRPCThresholdTimeSeconds;
				return true;
			}
		}
		return false;
	}
private:
	float ServerRPCAccumulatedTimeSeconds = 0.f;
	float ServerRPCThresholdTimeSeconds = 1.f / 999.f; // Default is to send at a max of 999hz. This part of the system needs to be build out more (better handling of super high FPS clients and fixed rate servers)

	// ------------------------------------------------------------------
	//	Debugging
	// ------------------------------------------------------------------
public:

#if NETSIM_MODEL_DEBUG
	TReplicationBuffer<TDebugState>* GetDebugBuffer() {	return &Buffers.Debug; }
	TDebugState* GetNextDebugStateWrite() { return Buffers.Debug.GetWriteNext(); }
	TNetworkSimBufferContainer<TBufferTypes>* GetHistoricBuffers(bool bCreate=false)
	{
		if (HistoricBuffers.IsValid() == false && bCreate) { HistoricBuffers.Reset(new TNetworkSimBufferContainer<TBufferTypes>()); }
		return HistoricBuffers.Get();
	}
#else
	TReplicationBuffer<TDebugState>* GetDebugBuffer(bool bCreate=false) {	return nullptr; }
	TDebugState* GetNextDebugStateWrite() { return nullptr; }
	TNetworkSimBufferContainer<TBufferTypes>* GetHistoricBuffers() { return nullptr; }
#endif

private:

#if NETSIM_MODEL_DEBUG
	TRepProxyDebug RepProxy_Debug;
	TUniquePtr<TNetworkSimBufferContainer<TBufferTypes>> HistoricBuffers;
#endif
};

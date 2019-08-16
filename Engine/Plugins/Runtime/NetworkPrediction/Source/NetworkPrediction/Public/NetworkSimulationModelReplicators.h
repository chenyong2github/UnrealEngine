// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Net/UnrealNetwork.h" // For MakeRelative
#include "NetworkSimulationModelCvars.h"

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	CVars and compile time constants
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace NetworkSimulationModelCVars
{
	NETSIM_DEVCVAR_SHIPCONST_INT(EnableLocalPrediction, 1, "ns.EnableLocalPrediction", "Toggle local prediction.");
	NETSIM_DEVCVAR_SHIPCONST_INT(EnableSimulatedReconcile, 1, "ns.EnableSimulatedReconcile", "Toggle simulated proxy reconciliation.");
	NETSIM_DEVCVAR_SHIPCONST_INT(EnableSimulatedExtrapolation, 1, "ns.EnableSimulatedExtrapolation", "Toggle simulated proxy extrapolation.");
	NETSIM_DEVCVAR_SHIPCONST_INT(ForceReconcile, 0, "ns.ForceReconcile", "Forces reconcile even if state does not differ. E.g, force resimulation after every netupdate.");
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
//	Templated Replicators
//
//	
//
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

template<typename TBufferTypes, typename TTickSettings>
struct TReplicatorEmpty
{ 
	// Used for client shadowstate compares. Should just call GetDirtyCount() on the buffer you are replicating
	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const { return 0; }

	// NetSerialize: just serialize the network data. Don't run simulation steps. Every replicator will be NetSerialized before moving on to Reconcile phase.
	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo) { }

	// Reconcile: called after everyone has NetSerialized. "Get right with the server": this function is about reconciling what the server told you vs what you have extrapolated or forward predicted locally
	template<typename T, typename TDriver>
	void Reconcile(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo) { }

	template<typename T, typename TDriver, typename TTickParameters>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const TTickParameters& TickParameters) { }
};

// This is the "templated base class" for replicators but is not required (i.e., this not an official interface used by TNetworkedSimulation model. Just a base implementation you can start with)
template<typename TBufferTypes, typename TTickSettings>
struct TReplicatorBase
{
	using TInputCmd = typename TBufferTypes::TInputCmd;

	// Used for client shadowstate compares. Should just call GetDirtyCount() on the buffer you are replicating
	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const { return 0; }

	// NetSerialize: just serialize the network data. Don't run simulation steps. Every replicator will be NetSerialized before moving on to Reconcile phase.
	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo) { }

	// Reconcile: called after everyone has NetSerialized. "Get right with the server": this function is about reconciling what the server told you vs what you have extrapolated or forward predicted locally
	template<typename T, typename TDriver>
	void Reconcile(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo) { }

	// Called prior to input processing. This function must updated TickInfo to allow simulation time (from TickParameters) and to possibly get new input.
	template<typename T, typename TDriver, typename TTickParameters>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const TTickParameters& TickParameters)
	{
		// Accumulate local delta time into TickInfo
		TickInfo.GiveSimulationTime(TickParameters.LocalDeltaTimeSeconds);

		// See if we have sim time to spend (variable tick will always go through. fixed step will miss frames while accumulating)
		TNetworkSimTime<TTickSettings> DeltaSimTime = TickInfo.GetRemaningAllowedSimulationTime();
		if (DeltaSimTime.ToRealTimeSeconds() > 0)
		{
			if (TickParameters.bGenerateLocalInputCmds)
			{
				if (TInputCmd* InputCmd = Buffers.Input.GetWriteNext())
				{
					*InputCmd = TInputCmd();
					InputCmd->SetFrameDeltaTime(DeltaSimTime);
					Driver->ProduceInput(DeltaSimTime, *InputCmd);
					TickInfo.MaxAllowedInputKeyframe++;
				}
			}
			else
			{
				// Allowed to process all remaining commands (we will still check for frame time in the core input processing loop to prevent speed hacks)
				TickInfo.MaxAllowedInputKeyframe = Buffers.Input.GetHeadKeyframe();

				// Check for gaps in commands
				if ( TickInfo.LastProcessedInputKeyframe+1 < Buffers.Input.GetTailKeyframe() )
				{
					UE_LOG(LogNetworkSim, Warning, TEXT("::Tick missing inputcmds. LastProcessedInputKeyframe: %d. %s"), TickInfo.LastProcessedInputKeyframe, *Buffers.Input.GetBasicDebugStr());
					TickInfo.LastProcessedInputKeyframe = Buffers.Input.GetTailKeyframe()+1;
				}
			}
		}
	}
};

// -------------------------------------------------------------------------------------------------------
//	"Reusable" pieces: mainly for replicating data on specified buffers
// -------------------------------------------------------------------------------------------------------

// Just serializes simulation time. Can be disabled by Enabled templated parameter. Used as base class in other classes below.
template<typename TBufferTypes, typename TTickSettings, typename TBase=TReplicatorBase<TBufferTypes, TTickSettings>, bool Enabled=true>
struct TReplicator_SimTime : public TBase
{
	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo)
	{
		TBase::NetSerialize(P, Buffers, TickInfo);
		SerializedTime = TickInfo.TotalProcessedSimulationTime;
		SerializedTime.NetSerialize(P.Ar);
	}

	TNetworkSimTime<TTickSettings> SerializedTime;
};

// Enabled=false specialization: do nothing
template<typename TBufferTypes, typename TTickSettings, typename TBase>
struct TReplicator_SimTime<TBufferTypes, TTickSettings, TBase, false> : public TBase { };

// -------------------------------------------------------------------------------------------------------
//	
// -------------------------------------------------------------------------------------------------------

// Replicates a sequence of elements. i.e., "the last MaxNumElements".
// On the receiving side, we merge the sequence into whatever we have locally
// Keyframes are synchronized. SimTime is also serialized by default (change by changing TBase)
template<typename TBufferTypes, typename TTickSettings, ENetworkSimBufferTypeId BufferId, int32 MaxNumElements=3, typename TBase=TReplicator_SimTime<TBufferTypes, TTickSettings>>
struct TReplicator_Sequence : public TBase
{
	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const 
	{
		return Buffers.template Get<BufferId>().GetDirtyCount() ^ (TBase::GetProxyDirtyCount(Buffers) << 2); 
	}

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo)
	{
		TBase::NetSerialize(P, Buffers, TickInfo);

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
};

// Replicates only the latest single element in the selected buffer.
// Keyframe is not synchronized: the new element is just added to head.
// SimTime is serialized by default (change by changing TBase)
template<typename TBufferTypes, typename TTickSettings, ENetworkSimBufferTypeId BufferId, typename TBase=TReplicator_SimTime<TBufferTypes, TTickSettings>>
struct TReplicator_Single : public TBase
{
	using TState = typename TBufferTypes::template select_type<BufferId>::type;

	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{ 
		return Buffers.template Get<BufferId>().GetDirtyCount() ^ (TBase::GetProxyDirtyCount(Buffers) << 2); ; 
	}

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo)
	{
		TBase::NetSerialize(P, Buffers, TickInfo);

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

// -------------------------------------------------------------------------------------------------------
//	Role based Replicators: these replicators are meant to server specific roles
// -------------------------------------------------------------------------------------------------------

// Default Replicator for the Server
//	-Replicates the InputBuffer client->server
template<typename TBufferTypes, typename TTickSettings, ENetworkSimBufferTypeId BufferId=ENetworkSimBufferTypeId::Input, typename TBase=TReplicator_Sequence<TBufferTypes, TTickSettings, BufferId, 3>>
struct TReplicator_Server : public TBase
{

};

template<typename TBufferTypes, typename TTickSettings, typename TBase=TReplicator_Single<TBufferTypes, TTickSettings, ENetworkSimBufferTypeId::Sync>>
struct TReplicator_Simulated : public TBase
{
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;
	
	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{
		return Buffers.Sync.GetDirtyCount() ^ (TBase::GetProxyDirtyCount(Buffers) << 2); 
	}

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo)
	{
		TBase::NetSerialize(P, Buffers, TickInfo);

		// Temp: can go away once reconcile is called only after receiving packet
		if (P.Ar.IsLoading())
		{		
			bPendingReconciliation = true;
		}
	}

	template<typename T, typename TDriver>
	void Reconcile(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
	{
		if (!bPendingReconciliation)
			return;

		bPendingReconciliation = false;
		TInputCmd* LastCmd = Buffers.Input.GetElementFromHead(0);
		
		{
			// Right now NetSerialize can be called multiple times before Reconcile can get called. Eventually we'd like to make it 1:1.
			// Until then, lets handle multiple serializes.
			int32 DeltaKeyframes = Buffers.Sync.GetHeadKeyframe() - Buffers.Input.GetHeadKeyframe();
			if (!ensure(DeltaKeyframes >= 0 && DeltaKeyframes < 1000))
			{
				// We way out of sync if this gets hit but this will reset things
				Buffers.Input.ResetNextHeadKeyframe( Buffers.Sync.GetHeadKeyframe() );
			}
		}
		
		// Generate a new, fake, command since we just added a new sync state to head
		while (Buffers.Input.GetHeadKeyframe() < Buffers.Sync.GetHeadKeyframe())
		{
			Buffers.Input.GetWriteNext();
		}

		TNetworkSimTime<TTickSettings> DeltaSimTime = TickInfo.TotalProcessedSimulationTime - this->SerializedTime;
		if (DeltaSimTime > TNetworkSimTime<TTickSettings>::FromMSec(0) && NetworkSimulationModelCVars::EnableSimulatedReconcile())
		{
			// This is the "Simulated Reconcile" part:
			// We have extrapolated ahead of the server. The latest network update is now "in the past" from what we rendered last frame.
			// To avoid popping or visual artifact, we will make up the difference here since we know the extra simulation time of the server.
			// (this can easily happen: variance in latency may delay a packet and we may get ahead)
			TInputCmd* NewCmd = Buffers.Input.GetWriteNext();
			*NewCmd = LastCmd ? *LastCmd : TInputCmd();
			NewCmd->SetFrameDeltaTime(DeltaSimTime);

			TSyncState* PrevSyncState = Buffers.Sync.GetElementFromHead(0);
			TSyncState* NextSyncState = Buffers.Sync.GetWriteNext();

			TAuxState Junk;

			// TODO: log this guy
			//NextMotionState->VisualLog( FVisualLoggingParameters(Keyframe == LastKeyframeToProcess ? EVisualLoggingContext::LastMispredicted : EVisualLoggingContext::OtherMispredicted, Keyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);

			// Do the actual update
			T::Update(Driver, NewCmd->GetFrameDeltaTime(), *NewCmd, *PrevSyncState, *NextSyncState, Junk);
		}
		else
		{
			// We are taking the latest server update as head. So set our clocks to it.
			TickInfo.TotalProcessedSimulationTime = this->SerializedTime;
			TickInfo.TotalAllowedSimulationTime = this->SerializedTime;
		}

		// Set our LastProcessedInputKeyframe to fake that we handled it
		TickInfo.LastProcessedInputKeyframe = Buffers.Input.GetHeadKeyframe();
		TickInfo.MaxAllowedInputKeyframe = FMath::Max(TickInfo.MaxAllowedInputKeyframe, TickInfo.LastProcessedInputKeyframe);
	}

	template<typename T, typename TDriver, typename TTickParameters>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const TTickParameters& TickParameters)
	{
		if (NetworkSimulationModelCVars::EnableSimulatedExtrapolation())
		{
			TBase::template PreSimTick<T, TDriver, TTickParameters>(Driver, Buffers, TickInfo, TickParameters);
		}
	}

private:
	
	bool bPendingReconciliation = false;
};


/** Replicates TSyncState and does basic reconciliation. */
template<typename TBufferTypes, typename TTickSettings, typename TBase=TReplicatorBase<TBufferTypes, TTickSettings>>
struct TReplicator_Autonomous : public TBase
{
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;

	int32 GetLastSerializedKeyframe() const { return LastSerializedKeyframe; }
	bool IsReconcileFaultDetected() const { return bReconcileFaultDetected; }
	const TNetworkSimTime<TTickSettings>& GetLastSerializedSimTime() const { return SerializedTime; }

	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{
		return Buffers.Sync.GetDirtyCount() ^ (TBase::GetProxyDirtyCount(Buffers) << 2); ;
	}

	// --------------------------------------------------------------------
	//	NetSerialize
	// --------------------------------------------------------------------
	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo)
	{
		FArchive& Ar = P.Ar;

		const int32 SerializedHeadKeyframe = FNetworkSimulationSerialization::SerializeKeyframe(Ar, Buffers.Sync.GetHeadKeyframe());
		TSyncState* SerializedState = nullptr;

		// Serialize total simulation time. This isn't really necessary since we have the keyframe above. 
		SerializedTime = TickInfo.TotalProcessedSimulationTime;
		SerializedTime.NetSerialize(P.Ar);

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
				if (ClientExistingState->ShouldReconcile(*SerializedState) || (NetworkSimulationModelCVars::ForceReconcile() > 0))
				{
					UE_CLOG(!Buffers.Input.IsValidKeyframe(SerializedHeadKeyframe), LogNetworkSim, Error, TEXT("::NetSerialize: Client InputBuffer does not contain data for frame %d. {%s} {%s}"), SerializedHeadKeyframe, *Buffers.Input.GetBasicDebugStr(), *Buffers.Sync.GetBasicDebugStr());
					bPendingReconciliation =  true;
				}
			}
			else
			{
				if (SerializedHeadKeyframe < Buffers.Sync.GetTailKeyframe())
				{
					// Case 1: the serialized state is older than what we've kept in our buffer. A bigger buffer would solve this! (at the price of more resimulated frames to recover when this happens)
					// This is a reconcile fault and we just need to chill. We'll stop sending user commands until the cmds in flight flush through the system and we catch back up.
					bReconcileFaultDetected = true;
				}
				else
				{
					// Case 2: We've received a newer frame than what we've processed locally. This could happen if we are buffering our inputs locally (but still sending to the server) or just not predicting
					bPendingReconciliation =  true;
				}
			}
		}

		LastSerializedKeyframe = SerializedHeadKeyframe;
	}

	// --------------------------------------------------------------------
	//	Reconcile
	// --------------------------------------------------------------------
	template<typename T, typename TDriver>
	void Reconcile(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
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

		// -------------------------------------------------------------------------------------------------------------------------
		// Resimulate
		// -------------------------------------------------------------------------------------------------------------------------

		TSyncState* ClientSyncState = Buffers.Sync.FindElementByKeyframe( ReconciliationKeyframe );	

		ServerState->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::LastConfirmed, ReconciliationKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);

		if (ClientSyncState)
		{
			// Existing ClientSyncState, log it before overwriting it
			ClientSyncState->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::FirstMispredicted, ReconciliationKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);
		}
		else
		{
			// No existing state, so create add it explicitly
			Buffers.Sync.ResetNextHeadKeyframe( ReconciliationKeyframe );
			ClientSyncState = Buffers.Sync.GetWriteNext();
		}

		// Set client's sync state to the server version
		check(ClientSyncState);
		*ClientSyncState = *ServerState;

		// Set the canonical simulation time to what we received (we will advance it as we resimulate)
		TickInfo.TotalProcessedSimulationTime = SerializedTime;
		TickInfo.LastProcessedInputKeyframe = ReconciliationKeyframe;
		TickInfo.MaxAllowedInputKeyframe = FMath::Max(TickInfo.MaxAllowedInputKeyframe, TickInfo.LastProcessedInputKeyframe); // Make sure this doesn't lag behind. This is the only place we should need to do this.
		
		// Resimulate all user commands 
		const int32 LastKeyframeToProcess = TickInfo.MaxAllowedInputKeyframe;
		for (int32 Keyframe = ReconciliationKeyframe+1; Keyframe <= LastKeyframeToProcess; ++Keyframe)
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
			NextMotionState->VisualLog( FVisualLoggingParameters(Keyframe == LastKeyframeToProcess ? EVisualLoggingContext::LastMispredicted : EVisualLoggingContext::OtherMispredicted, Keyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);

			// Do the actual update
			T::Update(Driver, ResimulateCmd->GetFrameDeltaTime(), *ResimulateCmd, *PrevMotionState, *NextMotionState, *AuxState);
			
			// Update TickInfo
			TickInfo.TotalProcessedSimulationTime += ResimulateCmd->GetFrameDeltaTime();
			TickInfo.LastProcessedInputKeyframe = Keyframe;

			// Log out the newly predicted state that we got.
			NextMotionState->VisualLog( FVisualLoggingParameters(Keyframe == LastKeyframeToProcess ? EVisualLoggingContext::LastPredicted : EVisualLoggingContext::OtherPredicted, Keyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);
		}
	}

	// --------------------------------------------------------------------
	//	PreSimTick
	// --------------------------------------------------------------------
	template<typename T, typename TDriver, typename TTickParameters>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const TTickParameters& TickParameters)
	{
		// If we have a reconcile fault, we cannot continue on with the simulation until it clears itself out. This effectively drops the input time and does not sample new inputs
		if (bReconcileFaultDetected)
		{
			return;
		}

		const int32 StartingMaxAllowedInputKeyframe = TickInfo.MaxAllowedInputKeyframe;

		TBase::template PreSimTick<T, TDriver, TTickParameters>(Driver, Buffers, TickInfo, TickParameters);

		// If prediction is disabled we need to reset our MaxAllowedInputKeyframe so we don't process the command(s) generated above
		if (NetworkSimulationModelCVars::EnableLocalPrediction() == 0)
		{
			TickInfo.MaxAllowedInputKeyframe = StartingMaxAllowedInputKeyframe;
		}
	}

private:
	
	TReplicationBuffer<TSyncState> ReconciliationBuffer;
	TNetworkSimTime<TTickSettings> SerializedTime; // last serialized time keeper

	int32 LastSerializedKeyframe = -1;
	bool bPendingReconciliation = false;	// Reconciliation is pending: we need to reconcile state from the server that differs from the locally predicted state
	bool bReconcileFaultDetected = false;	// A fault was detected: we received state from the server that we are unable to reconcile with locally predicted state
};
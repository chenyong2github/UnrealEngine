// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

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

template<typename TBufferTypes, typename TTickSettings>
struct TReplicatorBase
{
	template<typename T, typename TDriver>
	void Reconcile(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo) { }

	template<typename T, typename TDriver>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, typename TTickSettings::TRealTime RealDeltaTimeSeconds) { }
};

/** Replicates only the latest element. Does not synchronize keyframe */
template<typename TBufferTypes, typename TTickSettings, ENetworkSimBufferTypeId BufferId, typename TBase=TReplicatorBase<TBufferTypes, TTickSettings>>
struct TReplicator_LatestOnly : public TBase
{
	using TState = typename TBufferTypes::template select_type<BufferId>::type;

	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{ 
		return Buffers.template Get<BufferId>().GetDirtyCount(); 
	}

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo)
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

template<typename TBufferTypes, typename TTickSettings, typename TBase=TReplicatorBase<TBufferTypes, TTickSettings>, bool IsFixed=TTickSettings::FixedStepMS==0>
struct TReplicator_SimulatedExtrapolatedReconciliar : public TBase
{
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	
	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{
		return Buffers.Sync.GetDirtyCount(); 
	}

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo)
	{
		FArchive& Ar = P.Ar;
		TSyncState* State = nullptr;
		if (Ar.IsSaving())
		{
			State = Buffers.Sync.GetElementFromHead(0);
			check(State); // We should not be here if the buffer is empty. Want to avoid serializing an "empty" flag at the top here.
			SerializedTime = TickInfo.TotalProcessedSimulationTime;
		}
		else
		{
			State = Buffers.Sync.GetWriteNext();
			bPendingReconciliation = true;
		}

		SerializedTime.NetSerialize(Ar);
		State->NetSerialize(Ar);
	}

	template<typename T, typename TDriver>
	void Reconcile(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
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

	template<typename T, typename TDriver>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, typename TTickSettings::TRealTime RealDeltaTimeSeconds)
	{
		// Extrapolation: still needs to be a setting/state somewhere that we can change from actor code
		if (TInputCmd* SynthesizedCmd = TickInfo.GetNextInputForWrite(Buffers))
		{
			// We will create a single cmd to simulate the remaining time
			auto TimeToSimulate = TickInfo.GetRemaningAllowedSimulationTime();
			//SynthesizedCmd->FrameDeltaTime;
		}
	}

private:
	
	TNetworkSimTime<TTickSettings> SerializedTime;
	bool bPendingReconciliation = false;
};

template<typename TBufferTypes, typename TTickSettings, typename TBase>
struct TReplicator_SimulatedExtrapolatedReconciliar<TBufferTypes, TTickSettings, TBase, true> : TReplicator_SimulatedExtrapolatedReconciliar<TBufferTypes, TTickSettings, TBase, false>
{
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;

	template<typename T, typename TDriver>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, typename TTickSettings::TRealTime RealDeltaTimeSeconds)
	{
		// Extrapolation: still needs to be a setting/state somewhere that we can change from actor code
		if (TInputCmd* SynthesizedCmd = TickInfo.GetNextInputForWrite(Buffers))
		{
			// We will create a single cmd to simulate the remaining time
			auto TimeToSimulate = TickInfo.GetRemaningAllowedSimulationTime();
			SynthesizedCmd->GetFrameDeltaTime();
		}
	}
};

/** Replicates the latest sequence of N elements. N is dynamic, not compiled in. */
template<typename TBufferTypes, typename TTickSettings, ENetworkSimBufferTypeId BufferId, typename TBase=TReplicatorBase<TBufferTypes, TTickSettings>>
struct TReplicator_DynamicSequence : public TBase
{
	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const 
	{
		return Buffers.template Get<BufferId>().GetDirtyCount(); 
	}
	
	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo)
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
template<typename TBufferTypes, typename TTickSettings, typename TBase=TReplicatorBase<TBufferTypes, TTickSettings>>
struct TReplicator_BasicReconciliar : public TBase
{
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;

	int32 GetLastSerializedKeyframe() const { return LastSerializedKeyframe; }
	bool IsReconcileFaultDetected() const { return bReconcileFaultDetected; }
	const TNetworkSimTime<TTickSettings>& GetLastSerializedSimTime() const { return SerializedTime; }

	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{
		return Buffers.Sync.GetDirtyCount();
	}

	// --------------------------------------------------------------------
	//	PreSimTick
	// --------------------------------------------------------------------
	template<typename T, typename TDriver>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, typename TTickSettings::TRealTime RealDeltaTimeSeconds)
	{
		// If we have a reconcile fault, we cannot continue on with the simulation until it clears itself out. This effectively drops the input time and does not sample new inputs
		if (bReconcileFaultDetected)
		{
			return;
		}

		// Accumulate real time into the simulation. This will results in more Allowed Simulation Time (or will accumulate internally until we are ready for the next tick)
		TickInfo.GiveSimulationTime(RealDeltaTimeSeconds);

		TNetworkSimTime<TTickSettings> DeltaSimTime = TickInfo.GetRemaningAllowedSimulationTime();
		if (DeltaSimTime.ToRealTimeSeconds() > 0)
		{
			// Get next input in buffer, clear it, set delta time, then send it to driver to sample local input
			if (auto* InputCmd = Buffers.Input.GetWriteNext())
			{
				*InputCmd = typename TBufferTypes::TInputCmd();
				InputCmd->SetFrameDeltaTime(DeltaSimTime);
				Driver->ProduceInput(DeltaSimTime, *InputCmd);
			}

			// If we are predicting, then we are allowed to process the new command
			if (NetworkSimulationModelCVars::EnableLocalPrediction)
			{
				TickInfo.MaxAllowedInputKeyframe = Buffers.Input.GetHeadKeyframe();
			}
		}
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
				if (ClientExistingState->ShouldReconcile(*SerializedState) || (NetworkSimulationModelCVars::ForceReconcile > 0))
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

private:
	
	TReplicationBuffer<TSyncState> ReconciliationBuffer;
	TNetworkSimTime<TTickSettings> SerializedTime; // last serialized time keeper

	int32 LastSerializedKeyframe = -1;
	bool bPendingReconciliation = false;	// Reconciliation is pending: we need to reconcile state from the server that differs from the locally predicted state
	bool bReconcileFaultDetected = false;	// A fault was detected: we received state from the server that we are unable to reconcile with locally predicted state
};
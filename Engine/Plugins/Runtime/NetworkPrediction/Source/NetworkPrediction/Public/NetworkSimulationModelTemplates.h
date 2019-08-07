// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkSimulationModel.h"

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	CVars and compile time constants
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

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

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	TReplicationBuffer
//
//	Generic Cyclic buffer. Has canonical head position. This is the "Client Frame" or "Keyframe" identifier used by the rest of the system.
//	Contract: Elements in buffer are contiguously valid. We don't allow "gaps" in the buffer!
//	Use ::GetWriteNext<T> to write to the buffer.
//	Use ::CreateIterator to iterate from Tail->Head
//
//	for (auto It = Buffer.CreateIterator(); It; ++It) {
//		FMyType* Element = It.Element();
//		int32 Keyframe = It.Keyframe();
//	}
//
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>
struct TReplicationBuffer
{
	// Set the buffer size. Resizing is supported but not optimized because we preserve head/tail iteration (not a simple memcopy since where buffer wraps around will change)
	// In general, avoid resizing outside of startup/initialization.
	void SetBufferSize(int32 NewMaxNumElements)
	{
		check(NewMaxNumElements >= 0);	
		if (Data.Num() == NewMaxNumElements)
		{
			return;
		}
		
		if (Data.Num() == 0)
		{
			Data.SetNum(NewMaxNumElements, false);
		}
		else
		{
			// Grow or shrink. This is far from optimal but this operation should be rare
			TReplicationBuffer<T> Old(MoveTemp(*this)); // Move old data to a copy
			Data.SetNum(NewMaxNumElements, false);
			ResetNextHeadKeyframe(Old.GetTailKeyframe(), true); // Reset so our next write is at the old copies tail keyframe

			for (auto It = Old.CreateIterator(); It; ++It)
			{
				T* WriteNext = GetWriteNext();
				T* ReadNext = It.Element();
				FMemory::Memcpy(WriteNext, ReadNext, sizeof(T));
			}

			// Blow away the temp copy so we don't run the dtor on it on scope end (because we memcpy'd it over, not copy assigned)
			Old.Data.Reset();
			Old.NumValidElements = 0;
		}
	}

	// Gets the oldest element in the buffer (or null)
	T* GetElementFromTail(int32 OffsetFromTail)
	{
		check(OffsetFromTail >= 0); // Must always pass in valid offset

		// Out of range. This is not fatal, just don't return the wrong element.
		if (NumValidElements <= OffsetFromTail)
		{
			return nullptr;
		}
		
		const int32 Tail = GetTailKeyframe();
		const int32 Position = Tail + OffsetFromTail;
		const int32 Offset = (Position % Data.Num());
		return &Data[Offset];
	}

	// Gets the newest element in the buffer (or null)
	T* GetElementFromHead(int32 OffsetFromHead)
	{
		check(OffsetFromHead >= 0); // Must always pass in valid offset

		// Out of range. This is not fatal, just don't return the wrong element.
		if (NumValidElements <= OffsetFromHead)
		{
			return nullptr;
		}
		
		const int32 Position = Head - OffsetFromHead;
		check(Position >= 0);
		const int32 Offset = (Position % Data.Num());
		return &Data[Offset];
	}

	const T* FindElementByKeyframe(int32 Key) const
	{
		return FindElementByKeyframeImpl(Key);
	}

	T* FindElementByKeyframe(int32 Key)
	{
		const T* Element = const_cast<TReplicationBuffer<T>*>(this)->FindElementByKeyframeImpl(Key);
		return const_cast<T*>(Element);
	}

	// Returns the next element for writing. The contents of this element are unknown (could be stale content!). This also advances the head. Note the element returned is immediately considered "valid"!
	T* GetWriteNext()
	{
		check(Data.Num() > 0); // Buffer must be initialized first
		
		++Head;
		++DirtyCount;
		NumValidElements = FMath::Min<int32>(NumValidElements+1, Data.Num());

		const int32 Offset = (Head % Data.Num());
		return &Data[Offset];
	}

	// Resets the head keyframen, so that NextHeadKeyframe will be the keyframe of the next GetWriteNext<>(). E.g, call this before you want to write to NextHeadKeyframe.
	// To remphasize: NextHeadKeyframe is where the NEXT write will go. NOT "where the current head is".
	// Contents will be preserved if possible. E.g, if NextHeadKeyframe-1 is a valid keyframe, it will remain valid. If its not (if this creates "gaps" in the buffer) then
	// the contents will effectively be cleared. bForceClearContents will force this behavior even if the previous keyframe was valid.
	void ResetNextHeadKeyframe(int32 NextHeadKeyframe=0, bool bForceClearContents=false)
	{
		const int32 NewHeadKeyframe = NextHeadKeyframe - 1;
		if (bForceClearContents || NewHeadKeyframe < GetTailKeyframe() || NewHeadKeyframe > GetHeadKeyframe())
		{
			NumValidElements = 0;
		}
		else
		{
			NumValidElements += (NewHeadKeyframe - GetHeadKeyframe());
			check(NumValidElements >= 0 && NumValidElements <= Data.Num());
		}

		Head =  NewHeadKeyframe;
		DirtyCount++;
	}

	// Copies the contents of the SourceBuffer into this buffer. This existing buffer's data will be preserved as much as possible, but the guarantee is that the SourceBuffer's
	// data will all be added to this buffer.
	
	// Example 1: Target={1...5}, Source={3...9}  --> Target={1...9}
	// Example 2: Target={1...5}, Source={7...9}  --> Target={7...9}
	// Exmaple 3: Target={6...9}, Source={1...4}  --> Target={1...4}
	void CopyAndMerge(const TReplicationBuffer<T>& SourceBuffer)
	{
		const int32 StartingHeadKeyframe = GetHeadKeyframe();

		ResetNextHeadKeyframe(SourceBuffer.GetTailKeyframe());
		for (int32 Keyframe = SourceBuffer.GetTailKeyframe(); Keyframe <= SourceBuffer.GetHeadKeyframe(); ++Keyframe)
		{
			T* TargetData = GetWriteNext();
			check(GetHeadKeyframe() == Keyframe);

			const T* SourceData = SourceBuffer.FindElementByKeyframe(Keyframe);
			check(SourceData != nullptr);

			*TargetData = *SourceData;
		}
	}

	FString GetBasicDebugStr() const
	{
		return FString::Printf(TEXT("Elements: [%d/%d]. Keyframes: [%d-%d]"), NumValidElements, Data.Num(), GetTailKeyframe(), GetHeadKeyframe());
	}

	int32 GetNumValidElements() const { return NumValidElements; }
	int32 GetMaxNumElements() const { return Data.Num(); }
	int32 GetHeadKeyframe() const { return Head; }
	int32 GetTailKeyframe() const { return Head - NumValidElements + 1; }
	bool IsValidKeyframe(int32 Keyframe) const { return Keyframe >= GetTailKeyframe() && Keyframe <= GetHeadKeyframe(); }
	int32 GetDirtyCount() const { return DirtyCount; }

	// Create an iterator from tail->head. Note that no matter what template class is, this WILL iterate correctly across the element. The templated type
	// is just for casting the return element type. E.g, it is fine to use <uint8> in generic code, this will still step by 'StructSize'.
	struct TGenericIterator
	{
		TGenericIterator(TReplicationBuffer<T>& InBuffer) : Buffer(InBuffer)
		{
			CurrentKeyframe = Buffer.GetTailKeyframe();
		}

		int32 Keyframe() const { return CurrentKeyframe; }
		T* Element() const { return Buffer.FindElementByKeyframe(CurrentKeyframe); }
		void operator++() { CurrentKeyframe++; }
		operator bool()	{ return CurrentKeyframe <= Buffer.GetHeadKeyframe(); }

	private:
		TReplicationBuffer<T>& Buffer;
		int32 CurrentKeyframe = -1;
	};
		
	TGenericIterator CreateIterator() { return TGenericIterator(*this); }

private:

	const T* FindElementByKeyframeImpl(int32 Key) const
	{
		const int32 RelativeToHead = Key - Head;
		if (RelativeToHead > 0 || RelativeToHead <= -NumValidElements)
		{
			return nullptr;
		}

		return &Data[Key % Data.Num()];
	}

	int32 Head = INDEX_NONE;
	int32 DirtyCount = 0;
	int32 NumValidElements = 0;

	TArray<T> Data;
};

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	Templated Replicators/Reconciliars
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

/** Replicates only the latest element. Does not synchronize keyframe */
template<typename TReplicated>
struct TReplicator_LatestOnly
{
	TReplicator_LatestOnly(TReplicationBuffer<TReplicated>& InBuffer) : Buffer(InBuffer) { }
	int32 GetProxyDirtyCount() const { return Buffer.GetDirtyCount(); }

	void NetSerialize(const FNetSerializeParams& P)
	{
		FArchive& Ar = P.Ar;
		TReplicated* State = nullptr;
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

private:
	TReplicationBuffer<TReplicated>& Buffer;
};

/** Replicates the latest sequence of N elements. N is dynamic, not compiled in. */
template<typename TReplicated>
struct TReplicator_DynamicSequence
{
	TReplicator_DynamicSequence(TReplicationBuffer<TReplicated>& InBuffer) : Buffer(InBuffer) { }
	int32 GetProxyDirtyCount() const { return Buffer.GetDirtyCount(); }

	// --------------------------------------------------------------------
	//	NetSerialize
	// --------------------------------------------------------------------
	void NetSerialize(const FNetSerializeParams& P)
	{
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
			TReplicated* Cmd = Ar.IsLoading() ? Buffer.GetWriteNext() : Buffer.FindElementByKeyframe(Keyframe);
			Cmd->NetSerialize(P);
		}
	}

private:

	TReplicationBuffer<TReplicated>& Buffer;
	uint8 MaxNumElements = 3;
};

/** Replicates TSyncState and does basic reconciliation */
template<typename TInputCmd, typename TSyncState, typename TAuxState>
struct TReplicator_BasicReconciliar
{
	TReplicator_BasicReconciliar(TReplicationBuffer<TInputCmd>& InInputBuffer, TReplicationBuffer<TSyncState>& InSyncBuffer, TReplicationBuffer<TAuxState>& InAuxBuffer)
		: InputBuffer(InInputBuffer), SyncBuffer(InSyncBuffer), AuxBuffer(InAuxBuffer) { }

	int32 GetLastSerializedKeyframe() const { return LastSerializedKeyframe; }
	bool IsReconcileFaultDetected() const { return bReconcileFaultDetected; }
	int32 GetProxyDirtyCount() const { return SyncBuffer.GetDirtyCount(); }

	// --------------------------------------------------------------------
	//	NetSerialize
	// --------------------------------------------------------------------
	void NetSerialize(const FNetSerializeParams& P)
	{
		FArchive& Ar = P.Ar;
		const int32 SerializedHeadKeyframe = FNetworkSimulationSerialization::SerializeKeyframe(Ar, SyncBuffer.GetHeadKeyframe());
		TSyncState* SerializedState = nullptr;

		if (Ar.IsSaving())
		{
			// Server serialize the latest state
			SerializedState = SyncBuffer.GetElementFromHead(0);
		}
		else
		{
			// Its possible we process two packets in between ticks. This isn't really a problem but should be rare.
			if (bPendingReconciliation)
			{
				UE_LOG(LogNetworkSim, Warning, TEXT("bPendingReconciliation while in ::NetSerialize. LastSerializedKeyframe: %d. New SerializedHeadKeyframe: %d."), LastSerializedKeyframe, SerializedHeadKeyframe);
			}

			// Lazy init the reconciliation buffer. We don't need this on the server/writing side. (Fixme, with templated approach we could easily spcialize this to an inline elment instead of buffer?)
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
			if (TSyncState* ClientExistingState = SyncBuffer.FindElementByKeyframe(SerializedHeadKeyframe))
			{
				if (ClientExistingState->ShouldReconcile(*SerializedState) || (NetworkSimulationModelCVars::ForceReconcile > 0))
				{
					UE_CLOG(!InputBuffer.IsValidKeyframe(SerializedHeadKeyframe), LogNetworkSim, Error, TEXT("::NetSerialize: Client InputBuffer does not contain data for frame %d. {%s} {%s}"), SerializedHeadKeyframe, *InputBuffer.GetBasicDebugStr(), *SyncBuffer.GetBasicDebugStr());
					bPendingReconciliation =  true;
				}
			}
			else
			{
				// We don't have corresponding local state. There are two cases:
				if (NetworkSimulationModelCVars::EnableLocalPrediction) // Fixme: this is awkward. Expected if player prediction is disabled but coupling like this feels bad.
				{
					UE_LOG(LogNetworkSim, Warning, TEXT("::NetSerialize Fault: MotionStateBuffer does not contain data for frame %d. [%d-%d]"), SerializedHeadKeyframe, SyncBuffer.GetTailKeyframe(), SyncBuffer.GetHeadKeyframe());
				}

				// Case 1: the serialized state is older than what we've kept in our buffer. A bigger buffer would solve this! (at the price of more resimulated frames to recover when this happens)
				if (SerializedHeadKeyframe < SyncBuffer.GetTailKeyframe())
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
					checkf(SerializedHeadKeyframe <= InputBuffer.GetHeadKeyframe(), TEXT("Received newer motionstate that doesn't correspond to valid input cmd. SerializedHeadKeyframe: %d. {%s} {%s}"),
						SerializedHeadKeyframe, *InputBuffer.GetBasicDebugStr(), *SyncBuffer.GetBasicDebugStr());

					SyncBuffer.ResetNextHeadKeyframe(SerializedHeadKeyframe);
					TSyncState* ClientMotionState = SyncBuffer.GetWriteNext();
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
	void Reconcile(TDriver* Driver)
	{
		if (bPendingReconciliation == false)
		{
			return;
		}
		bPendingReconciliation = false;
		check(ReconciliationBuffer.GetNumValidElements() == 1); // this function assumes only 1 element

		const int32 ReconciliationKeyframe = ReconciliationBuffer.GetHeadKeyframe();
		TSyncState* ServerState = ReconciliationBuffer.GetElementFromHead(0);

		TInputCmd* ClientInputCmd = InputBuffer.FindElementByKeyframe( ReconciliationKeyframe );
		if (ClientInputCmd == nullptr)
		{
			// Fault: no historic command for this frame to reconcile with.
			UE_LOG(LogNetworkSim, Error, TEXT("Client InputBuffer does not contain data for frame %d. {%s} {%s}"), ReconciliationKeyframe, *InputBuffer.GetBasicDebugStr(), *SyncBuffer.GetBasicDebugStr());
			return;
		}

		TSyncState* ClientSyncState = SyncBuffer.FindElementByKeyframe( ReconciliationKeyframe );

		// Should not be here if the client doesn't have a valid state. See ::NetSerialize
		checkf(ClientSyncState != nullptr, TEXT("SyncBuffer does not contain data for frame %d. %s"), ReconciliationKeyframe, *SyncBuffer.GetBasicDebugStr());

		// -------------------------------------------------------------------------------------------------------------------------
		// Resimulate
		// -------------------------------------------------------------------------------------------------------------------------

		ServerState->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::LastConfirmed, ReconciliationKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);
		ClientSyncState->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::FirstMispredicted, ReconciliationKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);

		TSyncState* LatestClient = SyncBuffer.GetElementFromHead(0);

		
		UE_LOG(LogNetworkSim, Warning, TEXT("Client misprediction. Frame: %d"), ReconciliationKeyframe);
		/*
		UE_LOG(LogNetworkSim, Warning, TEXT("  Client: %s"), *ClientMotionState->Location.ToString());
		UE_LOG(LogNetworkSim, Warning, TEXT("  Server: %s"), *ServerState->Location.ToString());
		UE_LOG(LogNetworkSim, Warning, TEXT("  LatestClient: %s"), *LatestClient->Location.ToString());
		*/

		// Copy authoritative state over client state (FIXME: we may want to store this off historically somewhere? Or will VLog be enough for debugging?)
		*ClientSyncState = *ServerState;
		
		// Resimulate up to our latest MotionStateBuffer frame. Note that we may have unprocessed user commands in the command buffer at this point.
		const int32 LatestKeyframe = SyncBuffer.GetHeadKeyframe();		
		for (int32 Keyframe = ReconciliationKeyframe+1; Keyframe <= LatestKeyframe; ++Keyframe)
		{
			// Keyframe is the frame we are resimulating right now.
			TInputCmd* ResimulateCmd	= InputBuffer.FindElementByKeyframe(Keyframe);
			TSyncState* PrevMotionState = SyncBuffer.FindElementByKeyframe(Keyframe - 1);
			TSyncState* NextMotionState = SyncBuffer.FindElementByKeyframe(Keyframe);
			
			check(ResimulateCmd);
			check(PrevMotionState);
			check(NextMotionState);

			// TEMP (Aux buffer not fully plumbed through the system yet)
			// ------------------------------------------------------
			TAuxState TempAuxState;
			TAuxState* AuxState = AuxBuffer.FindElementByKeyframe(Keyframe);
			if (AuxState == nullptr)
			{
				AuxState = &TempAuxState;
			}
			// ------------------------------------------------------

			// Log out the Mispredicted state that we are about to overwrite.
			NextMotionState->VisualLog( FVisualLoggingParameters(Keyframe == LatestKeyframe ? EVisualLoggingContext::LastMispredicted : EVisualLoggingContext::OtherMispredicted, Keyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);

			// Do the actual update
			T::Update(Driver, *ResimulateCmd, *PrevMotionState, *NextMotionState, *AuxState);

			// Log out the newly predicted state that we got.
			NextMotionState->VisualLog( FVisualLoggingParameters(Keyframe == LatestKeyframe ? EVisualLoggingContext::LastPredicted : EVisualLoggingContext::OtherPredicted, Keyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);
		}
	}

private:

	TReplicationBuffer<TInputCmd>& InputBuffer;
	TReplicationBuffer<TSyncState>& SyncBuffer;
	TReplicationBuffer<TAuxState>& AuxBuffer;
	TReplicationBuffer<TSyncState> ReconciliationBuffer;

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
	typename TInputCmd,
	typename TSyncedState,
	typename TAuxState,
	typename TDebugState = FNetSimProcessedFrameDebugInfo,
	typename TRepProxyServerRPC = TReplicator_DynamicSequence<TInputCmd>,
	typename TRepProxyAutonomous = TReplicator_BasicReconciliar<TInputCmd, TSyncedState, TAuxState>,
	typename TRepProxySimulated = TReplicator_LatestOnly<TSyncedState>,
	typename TRepProxyReplay = TReplicator_DynamicSequence<TSyncedState>,
	typename TRepProxyDebug = TReplicator_DynamicSequence<TDebugState>
>
class TNetworkedSimulationModel : public IReplicationProxy
{
public:

	class IDriver
	{
	public:
		virtual void InitSyncState(TSyncedState& OutSyncState) const = 0;
		virtual void SyncTo(const TSyncedState& SyncState) = 0;
	};

	struct FTickParameters
	{
		ENetRole Role;
		float LocalDeltaTimeSeconds;
	};	

	TNetworkedSimulationModel()
		: RepProxy_ServerRPC(InputBuffer), RepProxy_Autonomous(InputBuffer, SyncBuffer, AuxBuffer), RepProxy_Simulated(SyncBuffer), RepProxy_Replay(SyncBuffer)
#if NETSIM_MODEL_DEBUG
		, RepProxy_Debug(DebugBuffer)
#endif
	{
	}

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

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		//														Reconciliation
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		if (Parameters.Role == ROLE_AutonomousProxy)
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
			checkf(InputBuffer.GetHeadKeyframe() - LastProcessedInputKeyframe <= 1, TEXT("Client Input Processing is out of sync. LastProcessedInputKeyframe: %d. InputBuffer: [%d - %d]"), LastProcessedInputKeyframe, InputBuffer.GetTailKeyframe(), InputBuffer.GetHeadKeyframe());

			// We should be processing input from this GFrameNumber. If we aren't, then our DeltaTime is probably off (input cmd's delta time wont match the actual delta time of the frame we sim on which will cause artifacts).
			// We also want to avoid accidentally introducing input latency as a matter of principle (input latency bad)
			// We *can* support local input buffering in two ways (neither currently implemented):
			//	-Fixed time steps (delta times will be constant so buffering wont matter)
			//	-Variable time steps with "interpolation layer": we will need to be able to absorb fluctuations in frame rate so there will be render frames that doesn't process any new input and
			//		render frames that have to process multiple inputs. (E.g, the goal would be to always "see" an update every render frame, even if we can't actually tick a new sim frame every render frame. Interpolation would be needed to accomplish this)
			// If either of the above ideas are implemented, it will be done as an optional/templated parameter to the network sim class.
			checkf(LastLocalInputGFrameNumber == 0 || GFrameNumber == LastLocalInputGFrameNumber, TEXT("TNetworkedSimulationModel running on stale input. You may have an ordering issue between TNetworkedSimulationModel::GetNextInputForWrite and TNetworkedSimulationModel::Tick. "
				"GFrameNumber: %d LastLocalInputGFrameNumber: %d"), GFrameNumber, LastLocalInputGFrameNumber);

		
			// Note that is is important for the NetworkSimModel to control *when* the reconcile happens. The replicator objects themselves should never do
			// reconciliation inside a NetSerialze call. Tick may not be the permanent place for this, but it can't be in NetSerialize.				
			RepProxy_Autonomous.template Reconcile<T, TDriver>(Driver);

			// Don't process new user commands if we are dealing with a reconciliation fault. That means we weren't able to correlate our predicted position with what the
			// server last sent us. If we are hitting this, it means we are waiting for the in flight commands to flush through the system. The proxy rep object will handle recovering.
			if (RepProxy_Autonomous.IsReconcileFaultDetected())
			{
				AllowCmds = 0;
			}

			// Check SyncBuffer being ahead of processed Keyframes. This would happen in cases where we are either not predicting or are buffering our input locally
			// while sending latest cmds to the server. Essentially, we got the authoritative motion state from the server before we ran the simulation locally.
			if (SyncBuffer.GetHeadKeyframe() > LastProcessedInputKeyframe)
			{
				checkf(InputBuffer.IsValidKeyframe(SyncBuffer.GetHeadKeyframe()), TEXT("MotionState and InputCmd buffers are out of system. LastProcessedInputKeyframe: %d {%s} vs {%s}"),
					LastProcessedInputKeyframe, *SyncBuffer.GetBasicDebugStr(), *InputBuffer.GetBasicDebugStr());

				UE_LOG(LogNetworkSim, Warning, TEXT("Skipping local input frames because we have newer data in MotionStateBuffer. LastProcessedInputKeyframe: %d. {%s} {%s}"),
					LastProcessedInputKeyframe, *SyncBuffer.GetBasicDebugStr(), *InputBuffer.GetBasicDebugStr());

				LastProcessedInputKeyframe = SyncBuffer.GetHeadKeyframe();
			}

			if (NetworkSimulationModelCVars::EnableLocalPrediction == 0)
			{
				AllowCmds = 0; // Don't process any commands this frame
				LastProcessedInputKeyframe = InputBuffer.GetHeadKeyframe(); // Increment so we can accept a new command next frame
			}
		}

		if (Parameters.Role == ROLE_Authority)
		{
			if ( LastProcessedInputKeyframe+1 < InputBuffer.GetTailKeyframe() )
			{
				// We've missed commands
				UE_LOG(LogNetworkSim, Warning, TEXT("::Tick missing inputcmds. LastProcessedInputKeyframe: %d. %s"), LastProcessedInputKeyframe, *InputBuffer.GetBasicDebugStr());
				LastProcessedInputKeyframe = InputBuffer.GetTailKeyframe()+1;
			}
		}

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		//														Input Processing
		// -------------------------------------------------------------------------------------------------------------------------------------------------

		int32 NumProcessed = 0;
		while (AllowCmds-- > 0)
		{
			const int32 Keyframe = LastProcessedInputKeyframe+1;
			if (TInputCmd* NextCmd = InputBuffer.FindElementByKeyframe(Keyframe))
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

				if (SyncBuffer.GetHeadKeyframe() != LastProcessedInputKeyframe)
				{
					if (LastProcessedInputKeyframe != 0)
					{
						// This shouldn't happen, but is not fatal. We are reseting the motion state buffer.
						UE_LOG(LogNetworkSim, Warning, TEXT("Break in SyncState continuity. LastProcessedInputKeyframe: %d. SyncBuffer.GetHeadKeyframe(): %d. Role=%d"), LastProcessedInputKeyframe, SyncBuffer.GetHeadKeyframe(), (int32)Parameters.Role);
					}

					// We need an initial/current state. Get this from the sim driver
					SyncBuffer.ResetNextHeadKeyframe(LastProcessedInputKeyframe);
					TSyncedState* StartingState = SyncBuffer.GetWriteNext();
					Driver->InitSyncState(*StartingState);
					
				}

				TSyncedState* PrevSyncState = SyncBuffer.FindElementByKeyframe(LastProcessedInputKeyframe);
				TSyncedState* NextSyncState = SyncBuffer.GetWriteNext();

				check(PrevSyncState != nullptr);
				check(NextSyncState != nullptr);
				check(SyncBuffer.GetHeadKeyframe() == Keyframe);
				
				if (DebugState)
				{
					DebugState->ProcessedKeyframes.Add(Keyframe);
				}

				TAuxState AuxState; // Temp: aux buffer not implemented yet

				T::Update(Driver, *NextCmd, *PrevSyncState, *NextSyncState, AuxState);
				NumProcessed++;

				LastProcessedInputKeyframe = Keyframe;
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
			if (SyncBuffer.GetNumValidElements() > 0)
			{
				Driver->SyncTo(*SyncBuffer.GetElementFromHead(0));
			}
		}

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		//														Debug
		// -------------------------------------------------------------------------------------------------------------------------------------------------

		// Finish debug state buffer recording (what the server processed each frame)
		if (DebugState)
		{
			DebugState->LastProcessedKeyframe = LastProcessedInputKeyframe;
			DebugState->HeadKeyframe = InputBuffer.GetHeadKeyframe();
		}

		// Historical data recording (longer buffers for historical reference)
		if (FHistoricBuffers* HistoricData = GetHistoricBuffers())
		{
			HistoricData->InputBuffer.CopyAndMerge(InputBuffer);
			HistoricData->SyncBuffer.CopyAndMerge(SyncBuffer);
			HistoricData->AuxBuffer.CopyAndMerge(AuxBuffer);
		}
	}	
	
	void InitializeForNetworkRole(const ENetRole Role, const bool IsLocallyControlled, const FNetworkSimulationModelInitParameters& Parameters)
	{
		InputBuffer.SetBufferSize(Parameters.InputBufferSize);
		SyncBuffer.SetBufferSize(Parameters.SyncedBufferSize);
		AuxBuffer.SetBufferSize(Parameters.AuxBufferSize);

		if (GetDebugBuffer())
		{
			GetDebugBuffer()->SetBufferSize(Parameters.DebugBufferSize);
		}

		if (FHistoricBuffers* MyHistoricBuffers = GetHistoricBuffers(true))
		{
			MyHistoricBuffers->InputBuffer.SetBufferSize(Parameters.HistoricBufferSize);
			MyHistoricBuffers->SyncBuffer.SetBufferSize(Parameters.HistoricBufferSize);
			MyHistoricBuffers->AuxBuffer.SetBufferSize(Parameters.HistoricBufferSize);
		}

		if (IsLocallyControlled)
		{
			check(Parameters.InputBufferSize > 0); // If you tell me this is locally controlled, you need to have an input buffer.
			InitLocalInputBuffer();
		}
	}

	void NetSerializeProxy(EReplicationProxyTarget Target, const FNetSerializeParams& Params)
	{
		switch(Target)
		{
		case EReplicationProxyTarget::ServerRPC:
			RepProxy_ServerRPC.NetSerialize(Params);
			break;
		case EReplicationProxyTarget::AutonomousProxy:
			RepProxy_Autonomous.NetSerialize(Params);
			break;
		case EReplicationProxyTarget::SimulatedProxy:
			RepProxy_Simulated.NetSerialize(Params);
			break;
		case EReplicationProxyTarget::Replay:
			RepProxy_Replay.NetSerialize(Params);
			break;
		case EReplicationProxyTarget::Debug:
#if NETSIM_MODEL_DEBUG
			RepProxy_Debug.NetSerialize(Params);
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
			return RepProxy_ServerRPC.GetProxyDirtyCount();
		case EReplicationProxyTarget::AutonomousProxy:
			return RepProxy_Autonomous.GetProxyDirtyCount();
		case EReplicationProxyTarget::SimulatedProxy:
			return RepProxy_Simulated.GetProxyDirtyCount();
		case EReplicationProxyTarget::Replay:
			return RepProxy_Replay.GetProxyDirtyCount();
		case EReplicationProxyTarget::Debug:
#if NETSIM_MODEL_DEBUG
			return RepProxy_Debug.GetProxyDirtyCount();
#endif
		default:
			checkf(false, TEXT("Unknown: %d"), (int32)Target);
			return 0;
		};
	}
	
	TInputCmd* GetNextInputForWrite(float DeltaTime)
	{
		ensure(LastLocalInputGFrameNumber != GFrameNumber);
		LastLocalInputGFrameNumber = GFrameNumber;

		if (InputBuffer.GetHeadKeyframe() == LastProcessedInputKeyframe)
		{
			// Only return a cmd if we have processed the last one. This is a bit heavy handed but is a good practice to start with. We want buffering of input to be very explicit, never something that accidently happens.
			TInputCmd* Next = InputBuffer.GetWriteNext();
			Next->FrameDeltaTime = DeltaTime;
			return Next;
		}
		return nullptr;
	}

	void InitLocalInputBuffer()
	{
		// Buffer should also be empty before calling this
		check(InputBuffer.GetHeadKeyframe() == INDEX_NONE);

		// We want to start with an empty command in the input buffer. See notes in input buffer processing function.
		*InputBuffer.GetWriteNext() = TInputCmd();

		LastLocalInputGFrameNumber = 0;
	}

	int32 LastProcessedInputKeyframe = 0;
	uint32 LastLocalInputGFrameNumber = 0;			// Tracks the last time we accepted local input via GetNextInputForWrite. Used to guard against accidental input latency due to ordering of input/sim ticking.

	TReplicationBuffer<TInputCmd> InputBuffer;
	TReplicationBuffer<TSyncedState> SyncBuffer;
	TReplicationBuffer<TAuxState> AuxBuffer;

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
	struct FHistoricBuffers
	{
		TReplicationBuffer<TInputCmd> InputBuffer;
		TReplicationBuffer<TSyncedState> SyncBuffer;
		TReplicationBuffer<TAuxState> AuxBuffer;
	};

#if NETSIM_MODEL_DEBUG
	TReplicationBuffer<TDebugState>* GetDebugBuffer() {	return &DebugBuffer; }
	TDebugState* GetNextDebugStateWrite() { return DebugBuffer.GetWriteNext(); }
	FHistoricBuffers* GetHistoricBuffers(bool bCreate=false)
	{
		if (HistoricBuffers.IsValid() == false && bCreate) { HistoricBuffers.Reset(new FHistoricBuffers()); }
		return HistoricBuffers.Get();
	}
#else
	TReplicationBuffer<TDebugState>* GetDebugBuffer(bool bCreate=false) {	return nullptr; }
	TDebugState* GetNextDebugStateWrite() { return nullptr; }
	FHistoricBuffers* GetHistoricBuffers() { return nullptr; }
#endif

private:

#if NETSIM_MODEL_DEBUG
	TReplicationBuffer<TDebugState> DebugBuffer;
	TRepProxyDebug RepProxy_Debug;

	TUniquePtr<FHistoricBuffers> HistoricBuffers;
#endif
};

class INetworkedMotionOwnerProxy
{

};

/** This is the layer that is tied in with mothing a component around a world */
template <typename T, typename TInputState, typename TSyncedState, typename TAuxState, typename TDebugState>
class TNetworkedMotionModel : public TNetworkedSimulationModel<T, TInputState, TSyncedState, TAuxState, TDebugState>
{
public:
	TNetworkedMotionModel(INetworkedMotionOwnerProxy* OwnerProxy)
	{
	
	}
};


// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Net/UnrealNetwork.h" // For MakeRelative
#include "NetworkSimulationModelCVars.h"
#include "NetworkSimulationModelInterpolator.h"

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	CVars and compile time constants
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace NetworkSimulationModelCVars
{
	NETSIM_DEVCVAR_SHIPCONST_INT(EnableLocalPrediction, 1, "ns.EnableLocalPrediction", "Toggle local prediction.");
	NETSIM_DEVCVAR_SHIPCONST_INT(EnableSimulatedReconcile, 1, "ns.EnableSimulatedReconcile", "Toggle simulated proxy reconciliation.");
	NETSIM_DEVCVAR_SHIPCONST_INT(EnableSimulatedExtrapolation, 1, "ns.EnableSimulatedExtrapolation", "Toggle simulated proxy extrapolation.");
	NETSIM_DEVCVAR_SHIPCONST_INT(ForceReconcile, 0, "ns.ForceReconcile", "Forces reconcile even if state does not differ. E.g, force resimulation after every netupdate.");
	NETSIM_DEVCVAR_SHIPCONST_INT(ForceReconcileSingle, 0, "ns.ForceReconcileSingle", "Forces a since reconcile to happen on the next frame");
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

	// Called prior to input processing. This function must updated TickInfo to allow simulation time (from TickParameters) and to possibly get new input.
	template<typename T, typename TDriver>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const FNetSimTickParameters& TickParameters) { }

	// Called after input processing. Should finalize the frame and do any smoothing/interpolation. This function is not allowed to modify the buffers or tick state, or even call the simulation/Update function.
	template<typename TDriver>
	void PostSimTick(TDriver* Driver, const TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo, const FNetSimTickParameters& TickParameters) { }
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
	template<typename T, typename TDriver>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const FNetSimTickParameters& TickParameters)
	{
		// Accumulate local delta time into TickInfo
		TickInfo.GiveSimulationTime(TickParameters.LocalDeltaTimeSeconds);
	}

	// Called after input processing. Should finalize the frame and do any smoothing/interpolation. This function is not allowed to modify the buffers or tick state, or even call the simulation/Update function.
	template<typename TDriver>
	void PostSimTick(TDriver* Driver, const TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo, const FNetSimTickParameters& TickParameters)
	{
		// Sync to latest frame if there is any
		if (Buffers.Sync.GetNumValidElements() > 0)
		{
			Driver->FinalizeFrame(*Buffers.Sync.GetElementFromHead(0));
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
	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
	{
		TBase::NetSerialize(P, Buffers, TickInfo);
		SerializedTime = TickInfo.GetTotalProcessedSimulationTime();
		SerializedTime.NetSerialize(P.Ar);
	}

	FNetworkSimTime SerializedTime;
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

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
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
				UE_LOG(LogNetworkSim, Warning, TEXT("Fault: gap in received %s buffer. PrevHead: %d. Received: %d-%d. Reseting previous buffer contents"), *LexToString(BufferId), PrevHead, StartingKeyframe, HeadKeyframe);
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

		LastSerializedKeyframe = HeadKeyframe;
	}

	int32 GetLastSerializedKeyframe() const { return LastSerializedKeyframe; }

protected:

	int32 LastSerializedKeyframe = 0;
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

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
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
//	Role based Replicators: these replicators are meant to serve specific roles wrt how the simulation evolves
// -------------------------------------------------------------------------------------------------------

// Default Replicator for the Server: Replicates the InputBuffer client->server
template<typename TBufferTypes, typename TTickSettings, typename TBase=TReplicator_Sequence<TBufferTypes, TTickSettings, ENetworkSimBufferTypeId::Input, 3>>
struct TReplicator_Server : public TBase
{
	using TInputCmd = typename TBufferTypes::TInputCmd;

	template<typename T, typename TDriver>
	void Reconcile(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
	{
		// After receiving input, server may process up to the latest received frames.
		// (If we needed to buffer input server side for whatever reason, we would do it here)
		// (also note that we will implicitly guard against speed hacks int he core update loop by not processing cmds past what we have been "allowed")
		TickInfo.MaxAllowedInputKeyframe = Buffers.Input.GetHeadKeyframe();

		// Check for gaps in commands
		if ( TickInfo.LastProcessedInputKeyframe+1 < Buffers.Input.GetTailKeyframe() )
		{
			UE_LOG(LogNetworkSim, Warning, TEXT("TReplicator_Server::Reconcile missing inputcmds. LastProcessedInputKeyframe: %d. %s"), TickInfo.LastProcessedInputKeyframe, *Buffers.Input.GetBasicDebugStr());
			TickInfo.LastProcessedInputKeyframe = Buffers.Input.GetTailKeyframe()+1;
		}
	}

	template<typename T, typename TDriver>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const FNetSimTickParameters& TickParameters)
	{
		TickInfo.GiveSimulationTime(TickParameters.LocalDeltaTimeSeconds);

		if (TickParameters.bGenerateLocalInputCmds)
		{
			FNetworkSimTime DeltaSimTime = TickInfo.GetRemainingAllowedSimulationTime();
			if (DeltaSimTime.IsPositive())
			{
				if (TInputCmd* InputCmd = Buffers.Input.GetWriteNext())
				{
					*InputCmd = TInputCmd();
					InputCmd->SetFrameDeltaTime(DeltaSimTime);
					Driver->ProduceInput(DeltaSimTime, *InputCmd);
					TickInfo.MaxAllowedInputKeyframe = Buffers.Input.GetHeadKeyframe();
				}
			}
		}
	}
};

/** Simulated: "non locally controlled" simulations. We support "Simulation Extrapolation" here (using the sim to fake inputs to advance the sim)  */
template<typename TBufferTypes, typename TTickSettings, typename TBase=TReplicatorEmpty<TBufferTypes, TTickSettings>>
struct TReplicator_Simulated : public TBase
{
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;

	// Parent Simulation. If this is set, this simulation will forward predict in sync with this parent sim. The parent sim should be an autonomous proxy driven simulation
	INetworkSimulationModel* ParentSimulation = nullptr;

	// Instance flag for enabling simulated extrapolation
	bool bAllowSimulatedExtrapolation = true;

	// Interpolated that will be used if bAllowSimulatedExtrapolation == false && ParentSimulation == nullptr
	TInterpolator<TBufferTypes, TTickSettings> Interpolator;

	// Last Serialized time and state
	FNetworkSimTime GetLastSerializedSimulationTime() const { return LastSerializedSimulationTime; }
	const TSyncState& GetLastSerializedSyncState() const { return LastSerializedSyncState; }
	
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
	
	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{
		return Buffers.Sync.GetDirtyCount() ^ (TBase::GetProxyDirtyCount(Buffers) << 2); 
	}

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
	{
		FArchive& Ar = P.Ar;

		FNetworkSimTime PrevLastSerializedSimulationTime = LastSerializedSimulationTime;

		// Serialize latest simulation time
		LastSerializedSimulationTime = TickInfo.GetTotalProcessedSimulationTime();
		LastSerializedSimulationTime.NetSerialize(P.Ar);

		// Serialize latest element
		TSyncState* State = nullptr;
		
		if (Ar.IsSaving())
		{
			State = Buffers.Sync.GetElementFromHead(0);
			check(State); // We should not be here if the buffer is empty. Want to avoid serializing an "empty" flag at the top here.
		}
		else
		{
			check(TickInfo.SimulationTimeBuffer.GetHeadKeyframe() == Buffers.Sync.GetHeadKeyframe());
			check(TickInfo.GetTotalProcessedSimulationTime() <= TickInfo.GetTotalAllowedSimulationTime());

			// Cache off our "starting" time before possibly overwriting it. We will use this in Reconcile to catch back up in some cases.
			if (TickInfo.GetTotalProcessedSimulationTime() > ReconcileSimulationTime)
			{
				ReconcileSimulationTime = TickInfo.GetTotalProcessedSimulationTime();
			}

			// Find out where this should go in the local buffer based on the serialized time
			int32 DestinationKeyframe = INDEX_NONE;
			if (LastSerializedSimulationTime > TickInfo.GetTotalProcessedSimulationTime())
			{
				// We are getting new state that is ahead of what we have locally, so it can safety go right to head
				DestinationKeyframe = TickInfo.SimulationTimeBuffer.GetHeadKeyframe()+1;
			}
			else
			{
				// We are getting state that is behind what we have locally
				for (int32 Keyframe = TickInfo.SimulationTimeBuffer.GetHeadKeyframe(); Keyframe >= TickInfo.SimulationTimeBuffer.GetTailKeyframe(); --Keyframe)
				{
					if (LastSerializedSimulationTime > *TickInfo.SimulationTimeBuffer.FindElementByKeyframe(Keyframe))
					{
						DestinationKeyframe = Keyframe+1;
						break;
					}
				}

				if (DestinationKeyframe == INDEX_NONE)
				{
					FNetworkSimTime TotalTimeAhead = *TickInfo.SimulationTimeBuffer.GetElementFromHead(0) - LastSerializedSimulationTime;
					FNetworkSimTime SerializeDelta = LastSerializedSimulationTime - PrevLastSerializedSimulationTime;

					// We are way far ahead of the server... we will need to clear out sync buffers, take what they gave us, and catch up in reconcile
					//UE_LOG(LogNetworkSim, Warning, TEXT("!!! TReplicator_Simulated. Large gap detected. SerializedTime: %s. Buffer time: [%s-%s]. %d Elements. DeltaFromHead: %s. DeltaSerialize: %s"), *LastSerializedSimulationTime.ToString(), 
					//	*TickInfo.SimulationTimeBuffer.GetElementFromTail(0)->ToString(), *TickInfo.SimulationTimeBuffer.GetElementFromHead(0)->ToString(), TickInfo.SimulationTimeBuffer.GetNumValidElements(), *TotalTimeAhead.ToString(), *SerializeDelta.ToString());
					DestinationKeyframe = TickInfo.SimulationTimeBuffer.GetHeadKeyframe()+2; // (Skip ahead 2 to force a break in continuity)
				}
			}

			check(DestinationKeyframe != INDEX_NONE);

			// "Finalize" our buffers and time keeping such that we serialize the latest state from the server in the right spot			
			Buffers.Sync.ResetNextHeadKeyframe(DestinationKeyframe);
			State = Buffers.Sync.GetWriteNext();

			// Update tick info
			TickInfo.SetTotalProcessedSimulationTime(LastSerializedSimulationTime, DestinationKeyframe);
			if (TickInfo.GetTotalAllowedSimulationTime() < LastSerializedSimulationTime)
			{
				TickInfo.SetTotalAllowedSimulationTime(LastSerializedSimulationTime);
			}

			check(TickInfo.GetTotalProcessedSimulationTime() <= TickInfo.GetTotalAllowedSimulationTime());

			TickInfo.LastProcessedInputKeyframe = DestinationKeyframe;
			TickInfo.MaxAllowedInputKeyframe = DestinationKeyframe;

			Buffers.Input.ResetNextHeadKeyframe(DestinationKeyframe+1);
		}

		check(State);
		State->NetSerialize(Ar);

		if (Ar.IsLoading())
		{
			LastSerializedSyncState = *State;
		}
	}

	template<typename T, typename TDriver>
	void Reconcile(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
	{
		if (ReconcileSimulationTime.IsPositive() == false)
		{
			return;
		}

		check(TickInfo.GetTotalProcessedSimulationTime() <= TickInfo.GetTotalAllowedSimulationTime());

		if (bAllowSimulatedExtrapolation && ParentSimulation == nullptr && NetworkSimulationModelCVars::EnableSimulatedExtrapolation() && NetworkSimulationModelCVars::EnableSimulatedReconcile())
		{
			TInputCmd* LastCmd = Buffers.Input.GetElementFromHead(0);

			// Simulated Reconcile requires the input buffer to be kept up to date with the Sync buffer
			// Generate a new, fake, command since we just added a new sync state to head
			while (Buffers.Input.GetHeadKeyframe() < Buffers.Sync.GetHeadKeyframe())
			{
				TInputCmd* Next = Buffers.Input.GetWriteNext();
				*Next = LastCmd ? *LastCmd : TInputCmd();
			}

			// Do we have time to make up? We may have extrapolated ahead of the server (totally fine - can happen with small amount of latency variance)
			FNetworkSimTime DeltaSimTime = ReconcileSimulationTime - TickInfo.GetTotalProcessedSimulationTime();
			if (DeltaSimTime.IsPositive() && NetworkSimulationModelCVars::EnableSimulatedReconcile())
			{
				SimulationExtrapolation<T, TDriver>(Driver, Buffers, TickInfo, DeltaSimTime);
			}
		}
		
		check(TickInfo.GetTotalProcessedSimulationTime() <= TickInfo.GetTotalAllowedSimulationTime());
		ReconcileSimulationTime.Reset();
	}

	template<typename T, typename TDriver>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const FNetSimTickParameters& TickParameters)
	{
		// Tick if we are dependent simulation or extrapolation is enabled
		if (ParentSimulation || (bAllowSimulatedExtrapolation && NetworkSimulationModelCVars::EnableSimulatedExtrapolation()))
		{
			// Don't start this simulation until you've gotten at least one update from the server
			if (TickInfo.GetTotalProcessedSimulationTime().IsPositive())
			{
				TickInfo.GiveSimulationTime(TickParameters.LocalDeltaTimeSeconds);
			}

			if (TickParameters.bGenerateLocalInputCmds)
			{
				FNetworkSimTime DeltaSimTime = TickInfo.GetRemainingAllowedSimulationTime();
				if (DeltaSimTime.IsPositive())
				{
					if (TInputCmd* InputCmd = Buffers.Input.GetWriteNext())
					{
						//UE_LOG(LogNetworkSim, Warning, TEXT("   Extrapolating %s"), *DeltaSimTime.ToString());

						*InputCmd = TInputCmd();
						InputCmd->SetFrameDeltaTime(DeltaSimTime);
						Driver->ProduceInput(DeltaSimTime, *InputCmd);
						TickInfo.MaxAllowedInputKeyframe = Buffers.Input.GetHeadKeyframe();
					}
				}
			}
		}
	}

	template<typename TDriver>
	void PostSimTick(TDriver* Driver, const TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo, const FNetSimTickParameters& TickParameters)
	{
		if (bAllowSimulatedExtrapolation || ParentSimulation)
		{
			// Sync to latest frame if there is any
			if (Buffers.Sync.GetNumValidElements() > 0)
			{
				Driver->FinalizeFrame(*Buffers.Sync.GetElementFromHead(0));
			}
			
		}
		else
		{
			Interpolator.template PostSimTick<TDriver>(Driver, Buffers, TickInfo, TickParameters);
		}
	}

	template<typename T, typename TDriver>
	void DependentRollbackBegin(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const FNetworkSimTime& RollbackDeltaTime, const int32 ParentKeyframe)
	{
		// For now, we make the assumption that our last serialized state and time match the parent simulation.
		// This would not always be the case with low frequency simulated proxies. But could be handled by replicating the simulations together (at the replication level)
		int32 NewHeadKeyframe = Buffers.Sync.GetHeadKeyframe()+1;
		Buffers.Sync.ResetNextHeadKeyframe(NewHeadKeyframe);
		Buffers.Input.ResetNextHeadKeyframe(NewHeadKeyframe);
		TickInfo.SetTotalProcessedSimulationTime(LastSerializedSimulationTime, NewHeadKeyframe);
		TickInfo.SetTotalAllowedSimulationTime(LastSerializedSimulationTime);

		*Buffers.Sync.GetWriteNext() = LastSerializedSyncState;
		*Buffers.Input.GetWriteNext() = TInputCmd();

		TickInfo.LastProcessedInputKeyframe = Buffers.Input.GetHeadKeyframe();
		TickInfo.MaxAllowedInputKeyframe = Buffers.Input.GetHeadKeyframe();

		Driver->FinalizeFrame(LastSerializedSyncState);

		LastSerializedSyncState.VisualLog( FVisualLoggingParameters(EVisualLoggingContext::FirstMispredicted, ParentKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver );
	}

	template<typename T, typename TDriver>
	void DependentRollbackStep(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const FNetworkSimTime& StepTime, const int32 ParentKeyframe, const bool bFinalStep)
	{
		TickInfo.SetTotalAllowedSimulationTime( TickInfo.GetTotalAllowedSimulationTime() + StepTime );

		SimulationExtrapolation<T, TDriver>(Driver, Buffers, TickInfo, StepTime);

		TSyncState* SyncState = Buffers.Sync.GetElementFromHead(0);
		check(SyncState);
		SyncState->VisualLog( FVisualLoggingParameters(bFinalStep ? EVisualLoggingContext::LastMispredicted : EVisualLoggingContext::OtherMispredicted, ParentKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver );
	}

private:

	template<typename T, typename TDriver>
	void SimulationExtrapolation(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const FNetworkSimTime DeltaSimTime)
	{
		TInputCmd* LastCmd = Buffers.Input.GetElementFromHead(0);

		// We have extrapolated ahead of the server. The latest network update is now "in the past" from what we rendered last frame.
		// We will insert a new keyframe to make up the difference from the last known state to where we want to be in the now.
				
		TInputCmd* NewCmd = Buffers.Input.GetWriteNext();
		*NewCmd = LastCmd ? *LastCmd : TInputCmd();
		NewCmd->SetFrameDeltaTime(DeltaSimTime);

		TSyncState* PrevSyncState = Buffers.Sync.GetElementFromHead(0);
		TSyncState* NextSyncState = Buffers.Sync.GetWriteNext();

		TAuxState Junk;

		// Do the actual update
		T::Update(Driver, NewCmd->GetFrameDeltaTime().ToRealTimeSeconds(), *NewCmd, *PrevSyncState, *NextSyncState, Junk);
		TickInfo.IncrementTotalProcessedSimulationTime(NewCmd->GetFrameDeltaTime(), Buffers.Sync.GetHeadKeyframe());

		// Set our LastProcessedInputKeyframe to fake that we handled it
		TickInfo.LastProcessedInputKeyframe = Buffers.Input.GetHeadKeyframe();
		TickInfo.MaxAllowedInputKeyframe = Buffers.Input.GetHeadKeyframe();
	}
	
	FNetworkSimTime ReconcileSimulationTime;
	FNetworkSimTime LastSerializedSimulationTime;
	TSyncState LastSerializedSyncState;
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
	const FNetworkSimTime& GetLastSerializedSimTime() const { return SerializedTime; }

	TArray<INetworkSimulationModel*> DependentSimulations;
	bool bDependentSimulationNeedsReconcile = false;

	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{
		return Buffers.Sync.GetDirtyCount() ^ (TBase::GetProxyDirtyCount(Buffers) << 2); ;
	}

	// --------------------------------------------------------------------
	//	NetSerialize
	// --------------------------------------------------------------------
	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
	{
		FArchive& Ar = P.Ar;

		const int32 SerializedHeadKeyframe = FNetworkSimulationSerialization::SerializeKeyframe(Ar, Buffers.Sync.GetHeadKeyframe());
		TSyncState* SerializedState = nullptr;

		// Serialize total simulation time. This isn't really necessary since we have the keyframe above. 
		SerializedTime = TickInfo.GetTotalProcessedSimulationTime();
		SerializedTime.NetSerialize(P.Ar);

		if (Ar.IsSaving())
		{
			// Server serialize the latest state
			SerializedState = Buffers.Sync.GetElementFromHead(0);
		}
		else
		{
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
				if (ClientExistingState->ShouldReconcile(*SerializedState) || (NetworkSimulationModelCVars::ForceReconcile() > 0) || (NetworkSimulationModelCVars::ForceReconcileSingle() > 0))
				{
					NetworkSimulationModelCVars::SetForceReconcileSingle(0);
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
		if (bPendingReconciliation == false && bDependentSimulationNeedsReconcile == false)
		{
			return;
		}
		bPendingReconciliation = false;
		bDependentSimulationNeedsReconcile = false;
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

		const bool bDoVisualLog = NetworkSimulationModelCVars::EnableLocalPrediction() > 0; // don't visual log if we have prediction disabled
		
		if (bDoVisualLog)
		{
			ServerState->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::LastConfirmed, ReconciliationKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);
		}

		if (ClientSyncState)
		{
			// Existing ClientSyncState, log it before overwriting it
			if (bDoVisualLog)
			{
				ClientSyncState->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::FirstMispredicted, ReconciliationKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);
			}
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

		const FNetworkSimTime RollbackDeltaTime = SerializedTime - TickInfo.GetTotalProcessedSimulationTime();

		// Set the canonical simulation time to what we received (we will advance it as we resimulate)
		TickInfo.SetTotalProcessedSimulationTime(SerializedTime, ReconciliationKeyframe);
		TickInfo.LastProcessedInputKeyframe = ReconciliationKeyframe;
		TickInfo.MaxAllowedInputKeyframe = FMath::Max(TickInfo.MaxAllowedInputKeyframe, TickInfo.LastProcessedInputKeyframe); // Make sure this doesn't lag behind. This is the only place we should need to do this.

		if (NetworkSimulationModelCVars::EnableLocalPrediction() == 0)
		{
			// If we aren't predicting at all, then we advanced the allowed sim time here, (since we aren't doing it in PreSimTick). This just keeps us constantly falling behind and not being able to toggle prediction on/off for debugging.
			TickInfo.SetTotalAllowedSimulationTime(SerializedTime);
		}

		// Tell dependent simulations to rollback
		for (INetworkSimulationModel* DependentSim : DependentSimulations)
		{
			DependentSim->BeginRollback(RollbackDeltaTime, ReconciliationKeyframe);
		}
		
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
			if (NextMotionState == nullptr)
			{
				// This should only happen if we transition from no local prediction to local prediction, since we will traverse through out input buffer but not have predicted sync states to look at.
				NextMotionState = Buffers.Sync.GetWriteNext();
				check(Keyframe == Buffers.Sync.GetHeadKeyframe());
			}

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
			T::Update(Driver, ResimulateCmd->GetFrameDeltaTime().ToRealTimeSeconds(), *ResimulateCmd, *PrevMotionState, *NextMotionState, *AuxState);
			
			// Update TickInfo
			TickInfo.IncrementTotalProcessedSimulationTime(ResimulateCmd->GetFrameDeltaTime(), Keyframe);
			TickInfo.LastProcessedInputKeyframe = Keyframe;

			// Log out the newly predicted state that we got.
			NextMotionState->VisualLog( FVisualLoggingParameters(Keyframe == LastKeyframeToProcess ? EVisualLoggingContext::LastPredicted : EVisualLoggingContext::OtherPredicted, Keyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);

			// Tell dependent simulations to advance
			for (INetworkSimulationModel* DependentSim : DependentSimulations)
			{
				DependentSim->StepRollback(ResimulateCmd->GetFrameDeltaTime(), Keyframe, (Keyframe == LastKeyframeToProcess));
			}
		}
	}

	// --------------------------------------------------------------------
	//	PreSimTick
	// --------------------------------------------------------------------
	template<typename T, typename TDriver>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const FNetSimTickParameters& TickParameters)
	{
		// If we have a reconcile fault, we cannot continue on with the simulation until it clears itself out. This effectively drops the input time and does not sample new inputs
		if (bReconcileFaultDetected)
		{
			return;
		}

		if (TickParameters.bGenerateLocalInputCmds)
		{
			if (NetworkSimulationModelCVars::EnableLocalPrediction() > 0)
			{
				// Prediction: add simulation time and generate new commands
				TickInfo.GiveSimulationTime(TickParameters.LocalDeltaTimeSeconds);
				const FNetworkSimTime DeltaSimTime = TickInfo.GetRemainingAllowedSimulationTime();
				if (DeltaSimTime.IsPositive())
				{
					if (TInputCmd* InputCmd = Buffers.Input.GetWriteNext())
					{
						*InputCmd = TInputCmd();
						InputCmd->SetFrameDeltaTime(DeltaSimTime);
						Driver->ProduceInput(DeltaSimTime, *InputCmd);
						TickInfo.MaxAllowedInputKeyframe = Buffers.Input.GetHeadKeyframe(); // Allow the new command to be processed by the local simulation
					}
				}
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
					if (TInputCmd* InputCmd = Buffers.Input.GetWriteNext())
					{
						*InputCmd = TInputCmd();
						InputCmd->SetFrameDeltaTime(NonPredictedInputTime);
						Driver->ProduceInput(NonPredictedInputTime, *InputCmd);
					}
				}
			}
		}
	}

private:
	
	TReplicationBuffer<TSyncState> ReconciliationBuffer;
	FNetworkSimTime SerializedTime; // last serialized time keeper
	TRealTimeAccumulator<TTickSettings> NonPredictedInputTimeAccumulator; // for tracking input time in the non predictive case

	int32 LastSerializedKeyframe = -1;
	bool bPendingReconciliation = false;	// Reconciliation is pending: we need to reconcile state from the server that differs from the locally predicted state
	bool bReconcileFaultDetected = false;	// A fault was detected: we received state from the server that we are unable to reconcile with locally predicted state
};

/** Special replicator for debug buffer, this preserves the local buffer and receives into a replicator-owned buffer (we want these buffers to be distinct/not merged) */
template<typename TBufferTypes, typename TTickSettings, int32 MaxNumElements=5, typename TBase=TReplicatorEmpty<TBufferTypes, TTickSettings>>
struct TReplicator_Debug : public TBase
{
	using TDebugState = typename TBufferTypes::TDebugState;

	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const 
	{
		return Buffers.Debug.GetDirtyCount() ^ (TBase::GetProxyDirtyCount(Buffers) << 2); 
	}

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
	{
		TBase::NetSerialize(P, Buffers, TickInfo);
		FArchive& Ar = P.Ar;

		TReplicationBuffer<TDebugState>& Buffer = Ar.IsSaving() ? Buffers.Debug : ReceivedBuffer;
		
		uint8 SerializedNumElements = FMath::Min<uint8>(MaxNumElements, Buffer.GetNumValidElements());
		Ar << SerializedNumElements;

		const int32 HeadKeyframe = FNetworkSimulationSerialization::SerializeKeyframe(Ar, Buffer.GetHeadKeyframe());
		const int32 StartingKeyframe = FMath::Max(0, HeadKeyframe - SerializedNumElements + 1);

		if (Ar.IsLoading())
		{
			// Lazy init on recieve
			if (ReceivedBuffer.GetMaxNumElements() != Buffers.Debug.GetMaxNumElements())
			{
				ReceivedBuffer.SetBufferSize(Buffers.Debug.GetMaxNumElements());
			}

			const int32 PrevHead = Buffer.GetHeadKeyframe();
			if (PrevHead < StartingKeyframe && PrevHead >= 0)
			{
				// There is a gap in the stream. In some cases, we want this to be a "fault" and bubble up. We may want to synthesize state or maybe we just skip ahead.
				UE_LOG(LogNetworkSim, Warning, TEXT("Fault: gap in received Debug buffer. PrevHead: %d. Received: %d-%d. Reseting previous buffer contents"), PrevHead, StartingKeyframe, HeadKeyframe);
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

	TReplicationBuffer<TDebugState> ReceivedBuffer;
};
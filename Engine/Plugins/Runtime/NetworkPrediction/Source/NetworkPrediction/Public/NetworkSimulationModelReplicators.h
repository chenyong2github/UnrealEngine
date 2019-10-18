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
		if (Buffers.Sync.Num() > 0)
		{
			Driver->FinalizeFrame(*Buffers.Sync.HeadElement(), *Buffers.Aux.HeadElement());
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
		uint8 SerializedNumElements = FMath::Min<uint8>(MaxNumElements, Buffer.Num());
		Ar << SerializedNumElements;

		const int32 HeadKeyframe = FNetworkSimulationSerialization::SerializeKeyframe(Ar, Buffer.HeadKeyframe());
		const int32 StartingKeyframe = FMath::Max(0, HeadKeyframe - SerializedNumElements + 1);

		if (Ar.IsLoading())
		{
			const int32 PrevHead = Buffer.HeadKeyframe();
			if (PrevHead < StartingKeyframe && PrevHead >= 0)
			{
				// There is a gap in the stream. In some cases, we want this to be a "fault" and bubble up. We may want to synthesize state or maybe we just skip ahead.
				UE_LOG(LogNetworkSim, Warning, TEXT("Fault: gap in received %s buffer. PrevHead: %d. Received: %d-%d. Reseting previous buffer contents"), *LexToString(BufferId), PrevHead, StartingKeyframe, HeadKeyframe);
			}
		}

		for (int32 Keyframe = StartingKeyframe; Keyframe <= HeadKeyframe; ++Keyframe)
		{
			// This, as is, is bad. The intention is that these functions serialize multiple items in some delta compressed fashion.
			// As is, we are just serializing the elements individually.
			auto* Cmd = Ar.IsLoading() ? Buffer.WriteKeyframe(Keyframe) : Buffer[Keyframe];
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

/** Helper that writes a new input cmd to the input buffer, at given Keyframe (usually the sim's PendingKeyframe). If keyframe doesn't exist, ProduceInput is called on the driver. */
template<typename TDriver, typename TBufferTypes>
void GenerateLocalInputCmdAtKeyframe(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, const FNetworkSimTime& DeltaSimTime, int32 Keyframe)
{
	using TInputCmd = typename TBufferTypes::TInputCmd;
	if (TInputCmd* InputCmd = Buffers.Input[Keyframe])
	{
		InputCmd->SetFrameDeltaTime(DeltaSimTime);
	}
	else
	{
		InputCmd = Buffers.Input.WriteKeyframe(Keyframe);
		*InputCmd = TInputCmd();
		InputCmd->SetFrameDeltaTime(DeltaSimTime);
		Driver->ProduceInput(DeltaSimTime, *InputCmd);
	}
}

/** Helper to generate a local input cmd if we have simulation time to spend and advance the simulation's MaxAllowedKeyframe so that it can be processed. */
template<typename TDriver, typename TBufferTypes, typename TTickSettings>
void TryGenerateLocalInput(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
{
	using TInputCmd = typename TBufferTypes::TInputCmd;

	FNetworkSimTime DeltaSimTime = TickInfo.GetRemainingAllowedSimulationTime();
	if (DeltaSimTime.IsPositive())
	{
		GenerateLocalInputCmdAtKeyframe(Driver, Buffers, DeltaSimTime, TickInfo.PendingKeyframe);
		TickInfo.MaxAllowedKeyframe = TickInfo.PendingKeyframe;
	}
}

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
		// (also note that we will implicitly guard against speed hacks in the core update loop by not processing cmds past what we have been "allowed")
		TickInfo.MaxAllowedKeyframe = Buffers.Input.HeadKeyframe();

		// Check for gaps in commands
		if ( TickInfo.PendingKeyframe < Buffers.Input.TailKeyframe() )
		{
			UE_LOG(LogNetworkSim, Warning, TEXT("TReplicator_Server::Reconcile missing inputcmds. PendingKeyframe: %d. %s. This can happen via packet loss"), TickInfo.PendingKeyframe, *Buffers.Input.GetBasicDebugStr());
			TickInfo.PendingKeyframe = Buffers.Input.TailKeyframe();
		}
	}

	template<typename T, typename TDriver>
	void PreSimTick(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const FNetSimTickParameters& TickParameters)
	{
		TickInfo.GiveSimulationTime(TickParameters.LocalDeltaTimeSeconds);
		if (TickParameters.bGenerateLocalInputCmds)
		{
			TryGenerateLocalInput(Driver, Buffers, TickInfo);
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
		TSyncState* SyncState = nullptr;
		TAuxState* AuxState = nullptr;
		
		if (Ar.IsSaving())
		{
			SyncState = Buffers.Sync.HeadElement();
			AuxState = Buffers.Aux.HeadElement();
			check(SyncState && AuxState	); // We should not be here if the buffer is empty. Want to avoid serializing an "empty" flag at the top here.
		}
		else
		{
			check(TickInfo.SimulationTimeBuffer.HeadKeyframe() == Buffers.Sync.HeadKeyframe());
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
				DestinationKeyframe = TickInfo.SimulationTimeBuffer.HeadKeyframe()+1;
			}
			else
			{
				// We are getting state that is behind what we have locally
				for (int32 Keyframe = TickInfo.SimulationTimeBuffer.HeadKeyframe(); Keyframe >= TickInfo.SimulationTimeBuffer.TailKeyframe(); --Keyframe)
				{
					if (LastSerializedSimulationTime > *TickInfo.SimulationTimeBuffer[Keyframe])
					{
						DestinationKeyframe = Keyframe+1;
						break;
					}
				}

				if (DestinationKeyframe == INDEX_NONE)
				{
					FNetworkSimTime TotalTimeAhead = TickInfo.GetTotalProcessedSimulationTime() - LastSerializedSimulationTime;
					FNetworkSimTime SerializeDelta = LastSerializedSimulationTime - PrevLastSerializedSimulationTime;

					// We are way far ahead of the server... we will need to clear out sync buffers, take what they gave us, and catch up in reconcile
					//UE_LOG(LogNetworkSim, Warning, TEXT("!!! TReplicator_Simulated. Large gap detected. SerializedTime: %s. Buffer time: [%s-%s]. %d Elements. DeltaFromHead: %s. DeltaSerialize: %s"), *LastSerializedSimulationTime.ToString(), 
					//	*TickInfo.SimulationTimeBuffer.GetElementFromTail(0)->ToString(), *TickInfo.SimulationTimeBuffer.GetElementFromHead(0)->ToString(), TickInfo.SimulationTimeBuffer.GetNumValidElements(), *TotalTimeAhead.ToString(), *SerializeDelta.ToString());
					DestinationKeyframe = TickInfo.SimulationTimeBuffer.HeadKeyframe()+2; // (Skip ahead 2 to force a break in continuity)
				}
			}

			check(DestinationKeyframe != INDEX_NONE);

			// "Finalize" our buffers and time keeping such that we serialize the latest state from the server in the right spot
			SyncState = Buffers.Sync.WriteKeyframe(DestinationKeyframe);
			AuxState = Buffers.Aux.WriteKeyframe(DestinationKeyframe);
			Buffers.Input.WriteKeyframe(DestinationKeyframe);

			// Update tick info
			TickInfo.SetTotalProcessedSimulationTime(LastSerializedSimulationTime, DestinationKeyframe);
			if (TickInfo.GetTotalAllowedSimulationTime() < LastSerializedSimulationTime)
			{
				TickInfo.SetTotalAllowedSimulationTime(LastSerializedSimulationTime);
			}

			check(TickInfo.GetTotalProcessedSimulationTime() <= TickInfo.GetTotalAllowedSimulationTime());

			TickInfo.PendingKeyframe = DestinationKeyframe;	// We are about to serialize state to DestinationKeyframe which will be "unprocessed" (has not been used to generate a new frame)
			TickInfo.MaxAllowedKeyframe = DestinationKeyframe-1; // Do not process PendingKeyframe on our account. ::PreSimTick will advance this based on our interpolation/extrapolation settings
		}

		check(SyncState && AuxState);
		SyncState->NetSerialize(Ar);
		AuxState->NetSerialize(Ar);

		if (Ar.IsLoading())
		{
			LastSerializedSyncState = *SyncState;
			LastSerializedAuxState = *AuxState;
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
			// Simulated Reconcile requires the input buffer to be kept up to date with the Sync buffer
			// Generate a new, fake, command since we just added a new sync state to head
			while (Buffers.Input.HeadKeyframe() < Buffers.Sync.HeadKeyframe())
			{
				TInputCmd* Next = Buffers.Input.WriteKeyframeInitializedFromHead(Buffers.Input.HeadKeyframe()+1);
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
				TryGenerateLocalInput(Driver, Buffers, TickInfo);
			}
		}
	}

	template<typename TDriver>
	void PostSimTick(TDriver* Driver, const TNetworkSimBufferContainer<TBufferTypes>& Buffers, const TSimulationTickState<TTickSettings>& TickInfo, const FNetSimTickParameters& TickParameters)
	{
		if (bAllowSimulatedExtrapolation || ParentSimulation)
		{
			// Sync to latest frame if there is any
			if (Buffers.Sync.Num() > 0)
			{
				Driver->FinalizeFrame(*Buffers.Sync.HeadElement(), *Buffers.Aux.HeadElement());
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
		const int32 NewHeadKeyframe = Buffers.Sync.HeadKeyframe()+1;
		
		TickInfo.SetTotalProcessedSimulationTime(LastSerializedSimulationTime, NewHeadKeyframe);
		TickInfo.SetTotalAllowedSimulationTime(LastSerializedSimulationTime);
		TickInfo.PendingKeyframe = NewHeadKeyframe;
		TickInfo.MaxAllowedKeyframe = NewHeadKeyframe;

		*Buffers.Sync.WriteKeyframe(NewHeadKeyframe) = LastSerializedSyncState;
		*Buffers.Input.WriteKeyframe(NewHeadKeyframe) = TInputCmd();

		Driver->FinalizeFrame(LastSerializedSyncState, LastSerializedAuxState);

		LastSerializedSyncState.VisualLog( FVisualLoggingParameters(EVisualLoggingContext::FirstMispredicted, ParentKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver );
	}

	template<typename T, typename TDriver>
	void DependentRollbackStep(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const FNetworkSimTime& StepTime, const int32 ParentKeyframe, const bool bFinalStep)
	{
		TickInfo.SetTotalAllowedSimulationTime( TickInfo.GetTotalAllowedSimulationTime() + StepTime );

		SimulationExtrapolation<T, TDriver>(Driver, Buffers, TickInfo, StepTime);

		TSyncState* SyncState = Buffers.Sync.HeadElement();
		check(SyncState);
		SyncState->VisualLog( FVisualLoggingParameters(bFinalStep ? EVisualLoggingContext::LastMispredicted : EVisualLoggingContext::OtherMispredicted, ParentKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver );
	}

private:

	template<typename T, typename TDriver>
	void SimulationExtrapolation(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo, const FNetworkSimTime DeltaSimTime)
	{
		// We have extrapolated ahead of the server. The latest network update is now "in the past" from what we rendered last frame.
		// We will insert a new keyframe to make up the difference from the last known state to where we want to be in the now.

		ensure(Buffers.Input.HeadKeyframe() == Buffers.Sync.HeadKeyframe());

		const int32 InputKeyframe = Buffers.Input.HeadKeyframe();
		const int32 OutputKeyframe = InputKeyframe + 1;

		// Create fake cmd				
		TInputCmd* NewCmd = Buffers.Input.WriteKeyframeInitializedFromHead(OutputKeyframe);
		NewCmd->SetFrameDeltaTime(DeltaSimTime);	
		
		// Create new sync state to write to
		TSyncState* PrevSyncState = Buffers.Sync[InputKeyframe];
		TSyncState* NextSyncState = Buffers.Sync.WriteKeyframe(OutputKeyframe);
		TAuxState* AuxState = Buffers.Aux[InputKeyframe];

		// Do the actual update
		{
			TScopedSimulationTick UpdateScope(TickInfo, OutputKeyframe, NewCmd->GetFrameDeltaTime());
			T::Update(Driver, NewCmd->GetFrameDeltaTime().ToRealTimeSeconds(), *NewCmd, *PrevSyncState, *NextSyncState, *AuxState, Buffers.Aux.WriteKeyframeFunc(OutputKeyframe));
		}

		TickInfo.MaxAllowedKeyframe = OutputKeyframe;
	}
	
	FNetworkSimTime ReconcileSimulationTime;
	FNetworkSimTime LastSerializedSimulationTime;
	TSyncState LastSerializedSyncState;
	TAuxState LastSerializedAuxState;	// Temp? This should be conditional or optional. We want to support not replicating the aux state to simulated proxies
};


/** Replicates TSyncState and does basic reconciliation. */
template<typename TBufferTypes, typename TTickSettings, typename TBase=TReplicatorBase<TBufferTypes, TTickSettings>>
struct TReplicator_Autonomous : public TBase
{
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;

	int32 GetLastSerializedKeyframe() const { return SerializedKeyframe; }
	bool IsReconcileFaultDetected() const { return bReconcileFaultDetected; }
	const FNetworkSimTime& GetLastSerializedSimTime() const { return SerializedTime; }

	TArray<INetworkSimulationModel*> DependentSimulations;
	bool bDependentSimulationNeedsReconcile = false;

	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const
	{
		return (Buffers.Sync.GetDirtyCount()) ^ (Buffers.Aux.GetDirtyCount() << 1) ^ (TBase::GetProxyDirtyCount(Buffers) << 2); 
	}

	// --------------------------------------------------------------------
	//	NetSerialize
	// --------------------------------------------------------------------
	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
	{
		FArchive& Ar = P.Ar;
		
		SerializedKeyframe = FNetworkSimulationSerialization::SerializeKeyframe(Ar, Buffers.Sync.HeadKeyframe());
		
		SerializedTime = TickInfo.GetTotalProcessedSimulationTime();
		SerializedTime.NetSerialize(P.Ar);

		if (Ar.IsSaving())
		{
			// Server serialize the latest state
			Buffers.Sync.HeadElement()->NetSerialize(Ar);
			Buffers.Aux.HeadElement()->NetSerialize(Ar);
		}
		else
		{
			SerializedSyncState.NetSerialize(Ar);
			SerializedAuxState.NetSerialize(Ar);
		}

		if (Ar.IsLoading())
		{
			bReconcileFaultDetected = false;
			bPendingReconciliation = false;

			// The state the client predicted that corresponds to the state the server just serialized to us
			if (TSyncState* ClientExistingState = Buffers.Sync[SerializedKeyframe])
			{
				// TODO: AuxState->ShouldReconcile
				if (ClientExistingState->ShouldReconcile(SerializedSyncState) || (NetworkSimulationModelCVars::ForceReconcile() > 0) || (NetworkSimulationModelCVars::ForceReconcileSingle() > 0))
				{
					NetworkSimulationModelCVars::SetForceReconcileSingle(0);
					UE_CLOG(!Buffers.Input.IsValidKeyframe(SerializedKeyframe-1), LogNetworkSim, Error, TEXT("::NetSerialize: Client InputBuffer does not contain data for frame %d. {%s} {%s}"), SerializedKeyframe, *Buffers.Input.GetBasicDebugStr(), *Buffers.Sync.GetBasicDebugStr());
					bPendingReconciliation =  true;
				}
			}
			else
			{
				if (SerializedKeyframe < Buffers.Sync.TailKeyframe())
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
	}

	// --------------------------------------------------------------------
	//	Reconcile
	// --------------------------------------------------------------------
	template<typename T, typename TDriver>
	void Reconcile(TDriver* Driver, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
	{
		if (bPendingReconciliation == false && (bDependentSimulationNeedsReconcile == false || SerializedKeyframe == INDEX_NONE))
		{
			return;
		}
		bPendingReconciliation = false;
		bDependentSimulationNeedsReconcile = false;

		TSyncState* ClientSyncState = Buffers.Sync[SerializedKeyframe];
		const bool bDoVisualLog = NetworkSimulationModelCVars::EnableLocalPrediction() > 0; // don't visual log if we have prediction disabled
		
		if (bDoVisualLog)
		{
			SerializedSyncState.VisualLog( FVisualLoggingParameters(EVisualLoggingContext::LastConfirmed, SerializedKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);
		}

		if (ClientSyncState)
		{
			// Existing ClientSyncState, log it before overwriting it
			if (bDoVisualLog)
			{
				ClientSyncState->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::FirstMispredicted, SerializedKeyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);
			}
		}
		else
		{
			// No existing state, so create add it explicitly
			ClientSyncState = Buffers.Sync.WriteKeyframe( SerializedKeyframe );
		}

		// Set client's sync state to the server version
		check(ClientSyncState);
		*ClientSyncState = SerializedSyncState;

		// Set Client's aux state to the server version
		*Buffers.Aux.WriteKeyframe(SerializedKeyframe) = SerializedAuxState;

		const FNetworkSimTime RollbackDeltaTime = SerializedTime - TickInfo.GetTotalProcessedSimulationTime();

		// Set the canonical simulation time to what we received (we will advance it as we resimulate)
		TickInfo.SetTotalProcessedSimulationTime(SerializedTime, SerializedKeyframe);
		TickInfo.PendingKeyframe = SerializedKeyframe;
		//TickInfo.MaxAllowedKeyframe = FMath::Max(TickInfo.MaxAllowedKeyframe, TickInfo.PendingKeyframe); // Make sure this doesn't lag behind. This is the only place we should need to do this.

		if (NetworkSimulationModelCVars::EnableLocalPrediction() == 0)
		{
			// If we aren't predicting at all, then we advanced the allowed sim time here, (since we aren't doing it in PreSimTick). This just keeps us constantly falling behind and not being able to toggle prediction on/off for debugging.
			TickInfo.SetTotalAllowedSimulationTime(SerializedTime);
		}

		// Tell dependent simulations to rollback
		for (INetworkSimulationModel* DependentSim : DependentSimulations)
		{
			DependentSim->BeginRollback(RollbackDeltaTime, SerializedKeyframe);
		}
		
		// Resimulate all user commands 
		const int32 LastKeyframeToProcess = TickInfo.MaxAllowedKeyframe;
		for (int32 Keyframe = SerializedKeyframe; Keyframe <= LastKeyframeToProcess; ++Keyframe)
		{
			const int32 OutputKeyframe = Keyframe+1;

			// Keyframe is the frame we are resimulating right now.
			TInputCmd* ResimulateCmd  = Buffers.Input[Keyframe];
			TAuxState* AuxState = Buffers.Aux[Keyframe];
			TSyncState* PrevSyncState = Buffers.Sync[Keyframe];
			TSyncState* NextSyncState = Buffers.Sync.WriteKeyframe(OutputKeyframe);
			
			check(ResimulateCmd);
			check(PrevSyncState);
			check(NextSyncState);
			check(AuxState);

			// Log out the Mispredicted state that we are about to overwrite.
			NextSyncState->VisualLog( FVisualLoggingParameters(Keyframe == LastKeyframeToProcess ? EVisualLoggingContext::LastMispredicted : EVisualLoggingContext::OtherMispredicted, Keyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);

			// Do the actual update
			{
				TScopedSimulationTick UpdateScope(TickInfo, OutputKeyframe, ResimulateCmd->GetFrameDeltaTime());
				T::Update(Driver, ResimulateCmd->GetFrameDeltaTime().ToRealTimeSeconds(), *ResimulateCmd, *PrevSyncState, *NextSyncState, *AuxState, Buffers.Aux.WriteKeyframeFunc(OutputKeyframe));
			}

			// Log out the newly predicted state that we got.
			NextSyncState->VisualLog( FVisualLoggingParameters(Keyframe == LastKeyframeToProcess ? EVisualLoggingContext::LastPredicted : EVisualLoggingContext::OtherPredicted, Keyframe, EVisualLoggingLifetime::Persistent), Driver, Driver);

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
				TryGenerateLocalInput(Driver, Buffers, TickInfo);
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
					GenerateLocalInputCmdAtKeyframe(Driver, Buffers, NonPredictedInputTime, Buffers.Input.HeadKeyframe() + 1);
				}
			}
		}
	}

private:
	
	TSyncState SerializedSyncState;
	TAuxState SerializedAuxState;
	FNetworkSimTime SerializedTime;
	int32 SerializedKeyframe = -1;

	bool bPendingReconciliation = false;	// Reconciliation is pending: we need to reconcile state from the server that differs from the locally predicted state
	bool bReconcileFaultDetected = false;	// A fault was detected: we received state from the server that we are unable to reconcile with locally predicted state

	TRealTimeAccumulator<TTickSettings> NonPredictedInputTimeAccumulator; // for tracking input time in the non predictive case
};

/** Special replicator for debug buffer, this preserves the local buffer and receives into a replicator-owned buffer (we want these buffers to be distinct/not merged) */
template<typename TBufferTypes, typename TTickSettings, int32 MaxNumElements=5, typename TBase=TReplicatorEmpty<TBufferTypes, TTickSettings>>
struct TReplicator_Debug : public TBase
{
	using TDebugState = typename TBufferTypes::TDebugState;
	using TDebugBuffer = typename TNetworkSimBufferContainer<TBufferTypes>::TDebugBuffer;

	int32 GetProxyDirtyCount(TNetworkSimBufferContainer<TBufferTypes>& Buffers) const 
	{
		return Buffers.Debug.GetDirtyCount() ^ (TBase::GetProxyDirtyCount(Buffers) << 2); 
	}

	void NetSerialize(const FNetSerializeParams& P, TNetworkSimBufferContainer<TBufferTypes>& Buffers, TSimulationTickState<TTickSettings>& TickInfo)
	{
		TBase::NetSerialize(P, Buffers, TickInfo);
		FArchive& Ar = P.Ar;

		TDebugBuffer& Buffer = Ar.IsSaving() ? Buffers.Debug : ReceivedBuffer;
		
		uint8 SerializedNumElements = FMath::Min<uint8>(MaxNumElements, Buffer.Num());
		Ar << SerializedNumElements;

		const int32 HeadKeyframe = FNetworkSimulationSerialization::SerializeKeyframe(Ar, Buffer.HeadKeyframe());
		const int32 StartingKeyframe = FMath::Max(0, HeadKeyframe - SerializedNumElements + 1);

		if (Ar.IsLoading())
		{
			const int32 PrevHead = Buffer.HeadKeyframe();
			if (PrevHead < StartingKeyframe && PrevHead >= 0)
			{
				// There is a gap in the stream. In some cases, we want this to be a "fault" and bubble up. We may want to synthesize state or maybe we just skip ahead.
				UE_LOG(LogNetworkSim, Warning, TEXT("Fault: gap in received Debug buffer. PrevHead: %d. Received: %d-%d. Reseting previous buffer contents"), PrevHead, StartingKeyframe, HeadKeyframe);
			}
		}

		for (int32 Keyframe = StartingKeyframe; Keyframe <= HeadKeyframe; ++Keyframe)
		{
			// This, as is, is bad. The intention is that these functions serialize multiple items in some delta compressed fashion.
			// As is, we are just serializing the elements individually.
			auto* Cmd = Ar.IsLoading() ? Buffer.WriteKeyframe(Keyframe) : Buffer[Keyframe];
			Cmd->NetSerialize(P);
		}
	}

	TDebugBuffer ReceivedBuffer;
};
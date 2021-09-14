// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ProfilingDebugging/CpuProfilerTrace.h"

/*=============================================================================

Note on ordering of operations and frame numbers:

[GT]
	Write data to next FNetworkPredictionSimCallbackInput as necessary
	Game Thread can know which Simulation Frame number the input will be processed on (FNetworkPredictionAsyncWorldManager::NextPhysicsStep)

[PT]
	CurrentPhysicsStep = N

	[Marshal Service]
		1. Apply Data from GT:
			a. New Instances
			b. State Mods
			c. InputCmds (locally controlled clients + remotely controlled on server... NOT remote controlled on clients this is done in Reconcile)

		<<< Data for frame SimulationFrame is now "finalized" >>>
			Think of it like "this is the data given to the [AsyncModelDef's] Tick function and Physics Simulation.

		2. Network Synchronization
			a. Clients apply the "finalized" data from the server here
		3. Game Thread Synchronization
			b. Clients and Server marshal data back to GT here

		4. Copy "finalized" data forward to next frame (LocalFrame + 1)

	[Tick Service]
		
		5. Run the user's Tick function (LocalFrame+1)			

	[Physics Engine]
	
		6. Step physics Simulation (rigid bodies, etc. This has nothing to do with NP gameplay data)


Its important to preserve the ordering above. Particular the "We apply all GT data and THEN "finalize" back to GT".
	-This allows us to "cover" all data modifications from GT->PT->GT
	-E.g, when we register a new instance on the GT, we marshal its creation to the PT, and marshal it back when the PT consumes its (rather than waiting for the next PT tick)
		-But we can also during creation, immediately write it to the GT output. (RegisterInstance_Impl in NetworkPredictionAsyncService_Registration.inl)
	-In other words, the GT can accurately "predict" its mods/new instances on the latest.
		-(note this prediction isn't perfect because we are writing to the previously marshalled frame on the GT, but it prevents "gaps" where the data/new instance isn't present)

That is the current thinking at least. The only alternative approach I see is to marshal back the previous frame's data without any of the latest GT data applied. This was the initial 
attempt but the "gap" mentioned above proved to be pretty annoying and something we had to guard against. 
	

=============================================================================*/

namespace UE_NP {

class IAsyncMarshalService_Internal
{
public:

	virtual ~IAsyncMarshalService_Internal() = default;
	virtual void MarshalPreSimulate(const int32 LocalFrame, const bool bIsResim, const FNetworkPredictionSimCallbackInput* SimInput, FNetworkPredictionSimCallbackOutput& Output) = 0;
};

template<typename AsyncModelDef>
class TAsyncMarshalService_Internal : public IAsyncMarshalService_Internal
{
	HOIST_ASYNCMODELDEF_TYPES()

public:

	TAsyncMarshalService_Internal(TAsyncModelDataStore_Internal<AsyncModelDef>* InDataStore)
	{
		npCheck(InDataStore != nullptr);
		DataStore = InDataStore;
	}

	// Marshal data from SimInput and to SimOutput, prior to ticking
	void MarshalPreSimulate(const int32 LocalFrame, const bool bIsResim, const FNetworkPredictionSimCallbackInput* SimInput, FNetworkPredictionSimCallbackOutput& SimOutput) final override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NPA_MarshalPreSimulate);

		const TAsyncModelDataStore_Input<AsyncModelDef>* InputData = SimInput->GetDataStore<AsyncModelDef>();
		TAsncFrameSnapshot<AsyncModelDef>& Snapshot = DataStore->Frames[LocalFrame];

		/*
		// Copy previous frame's contents
		if (LocalFrame > 0)
		{
			Snapshot.NetStates = DataStore->Frames[LocalFrame-1].NetStates;
			if (!bIsResim)
			{
				// Don't copy inputs forward during a resim
				// This is because non locally controlled sim inputs will be set in the Reconcile phase, 
				// and copying previous InputCmds would over-write that.
				Snapshot.InputCmds = DataStore->Frames[LocalFrame-1].InputCmds;
			}
		}
		*/

		// New instances
		for (const typename TAsyncModelDataStore_Input<AsyncModelDef>::FNewInstance& NewInstance : InputData->NewInstances)
		{
			DataStore->Instances.FindOrAdd(NewInstance.ID) = NewInstance.StaticData;

			NpResizeForIndex(Snapshot.InputCmds, NewInstance.StaticData.Index);
			NpResizeForIndex(Snapshot.NetStates, NewInstance.StaticData.Index);

			Snapshot.NetStates[NewInstance.StaticData.Index] = NewInstance.NetState;
		}

		// State Mods
		for (const TAsyncLocalStateMod<AsyncModelDef>& LocalMod : InputData->LocalStateMods)
		{
			LocalMod.Func(DataStore->Instances.FindChecked(LocalMod.ID).LocalState);
		}

		for (const TAsyncNetStateMod<AsyncModelDef>& NetMod : InputData->NetStateMods)
		{
			NetMod.Func(Snapshot.NetStates[NetMod.Index]);
		}

		// New Local InputCmds
		for (const typename TAsyncModelDataStore_Input<AsyncModelDef>::FLocalInputCmd& LocalInput : InputData->LocalInputCmds)
		{
			Snapshot.InputCmds[LocalInput.Index] = LocalInput.InputCmd;
		}

		// Deletes
		for (const FNetworkPredictionAsyncID& ID : InputData->DeletedInstances)
		{
			// I'm not sure if this is enough. Maybe we should track the 'kill frame' on the PT rather than removing anything from data structures
			DataStore->Instances.Remove(ID);
		}

		// Apply Corrections
		ApplyCorrections(LocalFrame);

		// Copy Snapshot back for GT
		TAsyncModelDataStore_Output<AsyncModelDef>* OutputData = SimOutput.GetDataStore<AsyncModelDef>();
		OutputData->Snapshot = Snapshot;

		// Copy snapshot state forward to next frame
		TAsncFrameSnapshot<AsyncModelDef>& NextSnapshot = DataStore->Frames[LocalFrame+1];
		NextSnapshot.NetStates = Snapshot.NetStates;
		if (!bIsResim)
		{
			// Don't copy inputs forward during a resim
			// This is because non locally controlled sim inputs will be set in the Reconcile phase, 
			// and copying previous InputCmds would over-write that.
			NextSnapshot.InputCmds = Snapshot.InputCmds;
		}
		else
		{
			// But even in resim we have to copy the AP input cmds forward due to how we are synchronizing with the GT and correction above.
			// Using the Input's LocalInputCmds is a bit abusive but is the simplest approach since the data is right there. See notes at 
			// the top of this header
			for (const typename TAsyncModelDataStore_Input<AsyncModelDef>::FLocalInputCmd& LocalInput : InputData->LocalInputCmds)
			{
				const int32 idx = LocalInput.Index;
				if (Snapshot.InputCmds.IsValidIndex(idx) && NextSnapshot.InputCmds.IsValidIndex(idx))
				{
					NextSnapshot.InputCmds[idx] = Snapshot.InputCmds[idx];
				}
			}
		}
	}

	void ApplyCorrections(int32 LocalFrame)
	{
		for (TConstSetBitIterator<> BitIt(DataStore->NetRecvData.PendingCorrectionMask); BitIt; ++BitIt)
		{
			const int32 idx = BitIt.GetIndex();
			const typename TAsyncNetRecvData<AsyncModelDef>::FInstance& RecvData = DataStore->NetRecvData.NetRecvInstances[idx];
			if (RecvData.Frame == LocalFrame)
			{
				TAsncFrameSnapshot<AsyncModelDef>& Snapshot = DataStore->Frames[LocalFrame];

				NpResizeForIndex(Snapshot.InputCmds, idx);
				NpResizeForIndex(Snapshot.NetStates, idx);
				
				Snapshot.InputCmds[idx] = RecvData.InputCmd;
				Snapshot.NetStates[idx] = RecvData.NetState;

				DataStore->NetRecvData.PendingCorrectionMask[idx] = false;
			}
		}
	}
	

protected:

	TAsyncModelDataStore_Internal<AsyncModelDef>* DataStore = nullptr;
};

// ------------------------------------------------------------------------------------------

class IAsyncMarshalService_External
{
public:

	virtual ~IAsyncMarshalService_External() = default;
	virtual void MarshalInput(const int32 Frame, FNetworkPredictionSimCallbackInput* SimInput) = 0;
	virtual void MarshalOutput(FNetworkPredictionSimCallbackOutput* SimOutput) = 0;
};


template<typename AsyncModelDef>
class TAsyncMarshalService_External : public IAsyncMarshalService_External
{
	HOIST_ASYNCMODELDEF_TYPES()

public:

	TAsyncMarshalService_External(TAsyncModelDataStore_External<AsyncModelDef>* InDataStore_External, TAsyncModelDataStore_Internal<AsyncModelDef>* InDataStore_Internal)
	{
		npCheck(InDataStore_External != nullptr);
		npCheck(InDataStore_Internal != nullptr);
		DataStore_External = InDataStore_External;
		DataStore_Internal = InDataStore_Internal;
	}

	// Marshal external data to SimInput
	void MarshalInput(const int32 Frame, FNetworkPredictionSimCallbackInput* SimInput) final override
	{
		TAsyncModelDataStore_Input<AsyncModelDef>* InputData = SimInput->GetDataStore<AsyncModelDef>();

		// ---------------------------------------------------------------------
		//	InputCmds
		// ---------------------------------------------------------------------

		for (TAsyncPendingInputCmdPtr<AsyncModelDef>& Info : DataStore_External->ActivePendingInputCmds)
		{
			npCheckSlow(Info.PendingInputCmd != nullptr);
			InputData->LocalInputCmds.Emplace(Info.Index, *Info.PendingInputCmd);
		}

		for (auto& MapIt : DataStore_External->PendingInputCmdBuffers)
		{
			TAsyncPendingInputCmdBuffer<AsyncModelDef>& PendingInputs = MapIt.Value.Get();
			InputData->LocalInputCmds.Emplace(PendingInputs.Index, PendingInputs.Buffer[Frame % PendingInputs.Buffer.Num()]);
		}

		// ---------------------------------------------------------------------
		//	Network
		// ---------------------------------------------------------------------

		if (DataStore_External->PendingNetRecv.NetRecvDirtyMask.Contains(true))
		{
			DataStore_Internal->NetRecvQueue.Enqueue(MoveTemp(DataStore_External->PendingNetRecv));
			DataStore_External->PendingNetRecv.Reset();
		}
	}

	// Marshal output data to external data store
	void MarshalOutput(FNetworkPredictionSimCallbackOutput* SimOutput) final override
	{
		TAsyncModelDataStore_Output<AsyncModelDef>* OutputData = SimOutput->GetDataStore<AsyncModelDef>();
		DataStore_External->LatestSnapshot = MoveTemp(OutputData->Snapshot);

		const int32 Num = FMath::Min<int32>(DataStore_External->LatestSnapshot.NetStates.Num(), DataStore_External->OutputWriteTargets.Num());
		for (int32 idx=0; idx < Num; ++idx)
		{
			if (NetStateType* OutNetState = DataStore_External->OutputWriteTargets[idx].OutNetState)
			{
				*OutNetState = DataStore_External->LatestSnapshot.NetStates[idx];
			}
		}
	}
	
	void ModifyLocalState(FNetworkPredictionAsyncID ID, TUniqueFunction<void(LocalStateType&)> Func, FNetworkPredictionSimCallbackInput* AsyncInput)
	{
		npCheckSlow(AsyncInput);
		if (!npEnsure(ID.IsValid()))
		{
			// We can't accept this state mod without a valid ID
			return;
		}

		if (ID.IsClientGenerated())
		{
			// We can't just drop this mod like we can for NetState since the local state is non replicable. We need to save it to a deferred data data structure
			DataStore_External->DeferredLocalStateMods.Emplace(ID, MoveTemp(Func));
		}
		else
		{
			AsyncInput->GetDataStore<AsyncModelDef>()->LocalStateMods.Emplace(ID, MoveTemp(Func));
		}
	}
	
	void ModifyNetState(FNetworkPredictionAsyncID ID, TUniqueFunction<void(NetStateType&)> Func, FNetworkPredictionSimCallbackInput* AsyncInput)
	{
		if (ID.IsClientGenerated() || !ID.IsValid())
		{
			// We don't have a valid server created ID yet, so we will just drop this on the floor.
			// It doesn't matter because GT mods are already "just guesses" wrt synchronization/networking 
			// and we will have authoritative server state repped to us when we get the ID anyways
			return;
		}

		npCheckSlow(AsyncInput);
		const int32 idx = DataStore_External->Instances.FindChecked(ID).Index;
		AsyncInput->GetDataStore<AsyncModelDef>()->NetStateMods.Emplace(idx, MoveTemp(Func));
	}

private:

	TAsyncModelDataStore_External<AsyncModelDef>* DataStore_External = nullptr;
	TAsyncModelDataStore_Internal<AsyncModelDef>* DataStore_Internal = nullptr;
};


} // namespace UE_NP
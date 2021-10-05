// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"
#include "RewindData.h"

#include "NetworkPhysics.h"
#include "NetworkPredictionLog.h"

#include "Async/NetworkPredictionAsyncInstanceData.h"
#include "Async/NetworkPredictionAsyncSimCallback.h"
#include "Async/NetworkPredictionAsyncID.h"
#include "Async/NetworkPredictionAsyncGlobalTickContext.h"

#include "Services/NetworkPredictionAsyncServiceRegistry.h"
#include "Services/NetworkPredictionAsyncService_Registration.inl"
#include "Services/NetworkPredictionAsyncService_Marshal.inl"
#include "Services/NetworkPredictionAsyncService_Ticking.inl"
#include "Services/NetworkPredictionAsyncService_Reconcile.inl"
#include "Services/NetworkPredictionAsyncService_NetSerialize.inl"



namespace UE_NP {

// -----------------------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------------------

class FNetworkPredictionAsyncSimCallback: public Chaos::TSimCallbackObject<FNetworkPredictionSimCallbackInput,FNetworkPredictionSimCallbackOutput>
{
public:

	virtual void OnPreSimulate_Internal() override
	{
		const FNetworkPredictionSimCallbackInput* Input = this->GetConsumerInput_Internal();
		if (Input == nullptr)
		{
			return;
		}

		Chaos::FPhysicsSolver* PhysicsSolver = static_cast<Chaos::FPhysicsSolver*>(this->GetSolver());
		if (PhysicsSolver->IsShuttingDown())
		{
			return;
		}

		const int32 LocalFrame = PhysicsSolver->GetCurrentFrame();
		const int32 SimulationFrame = LocalFrame - LocalFrameOffset;
		
		bool bIsResim = true;
		if (LocalFrame > LatestFrame)
		{
			bIsResim = false;
			LatestFrame = LocalFrame;
		}

		FNetworkPredictionSimCallbackOutput& Output = this->GetProducerOutputData_Internal();
		Output.SimulationFrame = SimulationFrame;
		Output.LocalFrameOffset = LocalFrameOffset;

		// Marshal all data from GT
		for (TUniquePtr<IAsyncMarshalService_Internal>& ServicePtr : MarshalServices.Array)
		{
			if (IAsyncMarshalService_Internal* Service = ServicePtr.Get())
			{
				Service->MarshalPreSimulate(LocalFrame, bIsResim, Input, Output);
			}
		}
		
		FNetworkPredictionAsyncTickContext Context(&DataStoreCollection);
		Context.World = World;
		Context.DeltaTimeSeconds = this->GetDeltaTime_Internal();
		Context.LocalStorageFrame = LocalFrame+1; // MarshalPreSimulate "finalized" LocalFrame and we are running the tick on the next frame's storage
		Context.SimulationFrame = SimulationFrame;
		Context.bIsResim = bIsResim;

		// Tick
		for (TUniquePtr<IAsyncTickingService>& ServicePtr : TickingServices.Array)
		{
			if (IAsyncTickingService* Service = ServicePtr.Get())
			{
				Service->Tick_Internal(Context, Output);
			}
		}
	}

	void ApplyCorrections_Internal(int32 PhysicsStep, Chaos::FSimCallbackInput* Input) override { } // REMOVE
	void FirstPreResimStep_Internal(int32 PhysicsStep) override { } // REMOVE
	int32 TriggerRewindIfNeeded_Internal(int32 LastCompletedStep) { return INDEX_NONE; } // REMOVE (as virtual on TSimCallbackObject)

	template<typename AsyncModelDef>
	void InstantiateServicesForModelDef()
	{
		TAsyncModelDataStore_Internal<AsyncModelDef>* DataStore = GetDataStore<AsyncModelDef>();
		MarshalServices.Instantiate<AsyncModelDef, TAsyncMarshalService_Internal<AsyncModelDef>>(DataStore);
		TickingServices.Instantiate<AsyncModelDef, TAsyncTickingService<AsyncModelDef>>(DataStore);
		ReconcileServices.Instantiate<AsyncModelDef, TAsyncReconcileService<AsyncModelDef>>(DataStore);
	}

	template<typename AsyncModelDef>
	TAsyncModelDataStore_Internal<AsyncModelDef>* GetDataStore()
	{
		return DataStoreCollection.GetDataStore<TAsyncModelDataStore_Internal<AsyncModelDef>, AsyncModelDef>();
	}

	void OnContactModification_Internal(const TArrayView<Chaos::FPBDCollisionConstraintHandleModification>& Modifications) final override { }

	int32 LatestFrame = -1; // Highest frame we've simmed. Used to detect resim
	UWorld* World = nullptr;

	// ------------------------------------------------------------------------------------

	int32 Reconcile_Internal(int32 LastCompletedStep)
	{
		Chaos::FPhysicsSolver* PhysicsSolver = static_cast<Chaos::FPhysicsSolver*>(this->GetSolver());
		npCheckSlow(PhysicsSolver);

		Chaos::FRewindData* RewindData = PhysicsSolver->GetRewindData();
		npCheckSlow(RewindData);

		const int32 EarliestFrame = RewindData->GetEarliestFrame_Internal();

		// pop latest localframe offset
		while(LocalFrameOffsetQueue.Dequeue(LocalFrameOffset));

		int32 RewindFrame = INDEX_NONE;
		for (TUniquePtr<IAsyncReconcileService>& ServicePtr : ReconcileServices.Array)
		{
			if (IAsyncReconcileService* Service = ServicePtr.Get())
			{
				const int32 Frame = Service->Reconcile(LastCompletedStep, EarliestFrame, LocalFrameOffset);
				if (Frame != INDEX_NONE)
				{
					RewindFrame = RewindFrame == INDEX_NONE ? Frame : FMath::Min<int32>(RewindFrame, Frame);
				}
			}
		}

		return RewindFrame;
	}

	void PushLatestLocalFrameOffset(int32 InLocalFrameOffset)
	{
		LocalFrameOffsetQueue.Enqueue(InLocalFrameOffset);
	}

private:

	TQueue<int32> LocalFrameOffsetQueue;
	int32 LocalFrameOffset = 0;

	TAsyncServiceStorage<IAsyncMarshalService_Internal> MarshalServices;
	TAsyncServiceStorage<IAsyncTickingService> TickingServices;
	TAsyncServiceStorage<IAsyncReconcileService> ReconcileServices;

	FAsyncDataStoreCollection DataStoreCollection;
};

// -----------------------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------------------

class FNetworkPredictionAsyncWorldManager: public INetworkPhysicsSubsystem
{
public:

	FNetworkPredictionAsyncWorldManager(UWorld* InWorld)
	{
		World = InWorld;
		if (ensure(World))
		{
			FPhysScene* PhysScene = World->GetPhysicsScene();
			if (ensureAlways(PhysScene))
			{
				Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();
				if (ensureAlways(Solver))
				{
					if (ensureAlways(Solver->GetRewindCallback()))
					{
						AsyncCallback = Solver->CreateAndRegisterSimCallbackObject_External<FNetworkPredictionAsyncSimCallback>(true, true);
						AsyncCallback->World = World;
					}
					else
					{
						UE_LOG(LogNetworkPhysics, Warning, TEXT("Rewind not enabled on Physics solver. FNetworkPredictionAsyncWorldManager will be disabled"));
					}
				}
			}
		}
	}

	bool RegisterInstance(FNetworkPredictionAsyncID& OutID, bool bForClient)
	{
		// Clients get temporary negative SpawnIDs so that they can initialize local state etc before finding out the replicated ID from the server
		OutID = FNetworkPredictionAsyncID(bForClient ? ClientSpawnIDCounter-- : SpawnIDCounter++);
		return true;
	}

	void UnregisterInstance(FNetworkPredictionAsyncID& ID)
	{
		npCheckSlow(AsyncCallback);
		FNetworkPredictionSimCallbackInput* Input = AsyncCallback->GetProducerInputData_External();
		npCheckSlow(Input);

		ForEachSim<IAsyncRegistrationService>(GlobalInstanceData_External.FindChecked(ID), RegistrationServices, [&](IAsyncRegistrationService* Service)
		{
			Service->UnregisterInstance(ID, Input);
		});

		GlobalInstanceData_External.FindAndRemoveChecked(ID);
		int32 idx = PlayerControllerMap.IndexOfByKey(ID);
		if (idx != INDEX_NONE)
		{
			PlayerControllerMap.RemoveAtSwap(idx, 1, false);
		}
		ID.Reset();
	}

	// Remaps the temporary, negative ClientID to the received serverID
	void RemapClientInstance(FNetworkPredictionAsyncID ClientID, FNetworkPredictionAsyncID ServerID)
	{
		npCheckSlow(AsyncCallback);
		FNetworkPredictionSimCallbackInput* Input = AsyncCallback->GetProducerInputData_External();
		npCheckSlow(Input);

		FGlobalInstanceData Data = GlobalInstanceData_External.FindAndRemoveChecked(ClientID);
		GlobalInstanceData_External.Emplace(ServerID, Data);

		if (FControllerInfo* ControllerInfo = PlayerControllerMap.FindByKey(ClientID))
		{
			ControllerInfo->ID = ServerID;
		}

		ForEachSim<IAsyncRegistrationService>(Data, RegistrationServices, [&](IAsyncRegistrationService* Service)
		{
			Service->RemapClientInstance(ClientID, ServerID, Input);
		});
	}

	template<typename AsyncModelDef>
	void RegisterSim(FNetworkPredictionAsyncID ID, typename AsyncModelDef::LocalStateType&& InitialValueLocal, typename AsyncModelDef::NetStateType&& InitialValueNet, 
		typename AsyncModelDef::InputCmdType* PendingInputCmd, typename AsyncModelDef::NetStateType* OutNetState)
	{
		// Ensure services are instantiated for this AsyncModelDef
		InstantiateServicesForModelDef<AsyncModelDef>();

		// Track that this ID is now participating in this sim
		npEnsureSlow(AsyncModelDef::ID < 32);
		GlobalInstanceData_External.FindOrAdd(ID).Sims |= (1 << AsyncModelDef::ID);

		// Call into RegistrationService
		npCheckSlow(AsyncCallback);
		FNetworkPredictionSimCallbackInput* Input = AsyncCallback->GetProducerInputData_External();
		npCheckSlow(Input);

		TAsyncRegistrationService<AsyncModelDef>* Service = (TAsyncRegistrationService<AsyncModelDef>*)RegistrationServices.Array[AsyncModelDef::ID].Get();
		Service->RegisterInstance(ID, Input, MoveTemp(InitialValueLocal), MoveTemp(InitialValueNet), PendingInputCmd, OutNetState);
	}

	template<typename AsyncModelDef>
	void UnregisterSim(FNetworkPredictionAsyncID ID)
	{
		// Call into RegistrationService
		npCheckSlow(AsyncCallback);
		FNetworkPredictionSimCallbackInput* Input = AsyncCallback->GetProducerInputData_External();
		npCheckSlow(Input);

		RegistrationServices.Array[AsyncModelDef::ID]->UnregisterInstance(ID, Input);

		GlobalInstanceData_External.FindOrAdd(ID).Sims &= ~(1 << AsyncModelDef::ID);
	}
	
	// Binds an NP instance to a PlayerController. PlayerController is required for networking, by setting a controlling PC the server will
	// accept InputCmds from this client.
	void RegisterController(FNetworkPredictionAsyncID ID, APlayerController* PC);

	void SetInputSource(FNetworkPredictionAsyncID ID, ENetworkPredictionAsyncInputSource Type, bool bIsLocal)
	{
		FGlobalInstanceData& InstanceData = GlobalInstanceData_External.FindChecked(ID);
		InstanceData.Flags = bIsLocal ? (InstanceData.Flags | (uint8)EInstanceFlags::LocallyControlled) : (InstanceData.Flags & ~(uint8)EInstanceFlags::LocallyControlled);

		ForEachSim<IAsyncRegistrationService>(GlobalInstanceData_External.FindChecked(ID), RegistrationServices, [&](IAsyncRegistrationService* Service)
		{
			Service->SetInputSource(ID, Type, bIsLocal);
		});
	}

	// Serializes a registered instance to the archive
	//	This includes the latest state/input + a "future input" which is the latest, but currently unprocessed, InputCmd we have on the GT
	void NetSerializeInstance(FNetworkPredictionAsyncID ID, FArchive& Ar)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NPA_NetSerializeInstance);

		FGlobalInstanceData& InstanceData = GlobalInstanceData_External.FindChecked(ID);

		// 1. Frame (this is the latest frame we have results for, on the game thread)
		int32 Frame = GlobalOutput_External.SimulationFrame;
		Ar << Frame;

		uint8 FutureDelta = 0;

		if (Ar.IsSaving())
		{
			// Find what the latest InputCmd frame we have for this Instance
			// (for Remote players will be InputBuffer's LastFrame)
			// (for local players it will be the PendingInputCmd/NextPhysicsStep)
			int32 FutureFrame = 0;
			FControllerInfo* ControllerInfo = PlayerControllerMap.FindByKey(ID);

			int32 PendingInputFrame = INDEX_NONE;
			
			if (ControllerInfo && ControllerInfo->InputSource == ENetworkPredictionAsyncInputSource::Buffered)
			{
				const int32 SignedFutureDelta = ControllerInfo->LastFrame - GlobalOutput_External.SimulationFrame;
				if (SignedFutureDelta > 0)
				{
					if (SignedFutureDelta < InputCmdBufferSize)
					{
						FutureDelta = (uint8)SignedFutureDelta;
						FutureFrame = ControllerInfo->LastFrame;
						PendingInputFrame = FutureFrame; // This is telling the NetSend call below to send this frame from the PendingInputCmd
					}
					else
					{
						UE_LOG(LogNetworkPrediction, Warning, TEXT("Too many buffered input cmds detected for client. LastFrame: %d SimulationFrame: %d. (Delta: %d)"), ControllerInfo->LastFrame, GlobalOutput_External.SimulationFrame, SignedFutureDelta);
					}
				}
			}
			else
			{
				const int32 SignedFutureDelta = this->NextPhysicsStep - GlobalOutput_External.SimulationFrame;
				if (npEnsure(SignedFutureDelta > 0) && npEnsure(SignedFutureDelta < InputCmdBufferSize))
				{
					FutureDelta = (uint8)SignedFutureDelta;
				}
			}

			// 2. Future Frame Delta (this is the latest frame we have input for, but haven't processed yet)
			Ar << FutureDelta;

			ForEachSim<INetSerializeService>(InstanceData, NetSerializeServices, [&](INetSerializeService* Service)
			{
				// 3. AsyncModelDef-specific payloads
				Service->NetSend(ID, InstanceData.Flags, Frame, PendingInputFrame, (FNetBitWriter&)Ar);
			});	
		}
		else
		{
			// 2. Future Frame Delta (this is a future frame that we have input for, but no NetState etc since the server has not processed this frame yet)
			Ar << FutureDelta;

			const int32 FutureFrame = Frame + (int32)FutureDelta;
			
			ForEachSim<INetSerializeService>(InstanceData, NetSerializeServices, [&](INetSerializeService* Service)
			{
				// 3. AsyncModelDef-specific payloads
				Service->NetRecv(ID, InstanceData.Flags, Frame, FutureFrame, (FNetBitReader&)Ar);
			});	
		}
	}

	template<typename AsyncModelDef>
	void ModifyLocalState(FNetworkPredictionAsyncID ID, TUniqueFunction<void(typename AsyncModelDef::LocalStateType&)> Func)
	{
		FNetworkPredictionSimCallbackInput* AsyncInput = AsyncCallback->GetProducerInputData_External();
		if (ensure(AsyncInput))
		{
			TAsyncMarshalService_External<AsyncModelDef>* Service = (TAsyncMarshalService_External<AsyncModelDef>*)MarshalServices.Array[AsyncModelDef::ID].Get();
			Service->ModifyLocalState(ID, MoveTemp(Func), AsyncInput);
		}
	}

	template<typename AsyncModelDef>
	void ModifyNetState(FNetworkPredictionAsyncID ID, TUniqueFunction<void(typename AsyncModelDef::NetStateType&)> Func)
	{
		FNetworkPredictionSimCallbackInput* AsyncInput = AsyncCallback->GetProducerInputData_External();
		if (ensure(AsyncInput))
		{
			TAsyncMarshalService_External<AsyncModelDef>* Service = (TAsyncMarshalService_External<AsyncModelDef>*)MarshalServices.Array[AsyncModelDef::ID].Get();
			Service->ModifyNetState(ID, MoveTemp(Func), AsyncInput);
		}
	}

	// -----------------------------------------------------------------------------------------------------
	// INetworkPhysicsSubsystem
	// -----------------------------------------------------------------------------------------------------

	static FName GetName() { return FName("NPAsync"); }
	static FNetworkPredictionAsyncWorldManager* Get(UWorld* World)
	{
		if (UNetworkPhysicsManager* NetworkPhysicsManager = World->GetSubsystem<UNetworkPhysicsManager>())
		{
			if (FNetworkPredictionAsyncWorldManager* Existing = NetworkPhysicsManager->GetSubsystem<FNetworkPredictionAsyncWorldManager>())
			{
				return Existing;
			}

			FNetworkPredictionAsyncWorldManager* NewInstance = NetworkPhysicsManager->RegisterSubsystem<FNetworkPredictionAsyncWorldManager>(MakeUnique<FNetworkPredictionAsyncWorldManager>(World));
			return NewInstance;
		}
		ensure(false);
		return nullptr;
	}

	void PostNetRecv(UWorld* InWorld, int32 LocalOffset, int32 LastProcessedFrame) override
	{
	}

	void PreNetSend(UWorld* InWorld, float DeltaSeconds) override
	{
	}

	void InjectInputs_External(int32 PhysicsStep, int32 NumSteps, int32 LocalFrameOffset, bool& bOutSendClientInputCmd) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NPA_ProcessInputs_External);

		npEnsureMsgf(NextPhysicsStep == 0 || (NextPhysicsStep == PhysicsStep), TEXT("Unexpected PhysicsStep %d (NextPhysicsStep: %d)"), PhysicsStep, NextPhysicsStep);
		NextPhysicsStep = PhysicsStep;

		// Prep the next AsyncInput
		npCheckSlow(AsyncCallback);
		FNetworkPredictionSimCallbackInput* AsyncInput = AsyncCallback->GetProducerInputData_External();
		npCheckSlow(AsyncInput);

		AsyncCallback->PushLatestLocalFrameOffset(LocalFrameOffset);

		for (int32 StepNum=0; StepNum < NumSteps; ++StepNum)
		{
			// Whatever pending data is in the AsyncInput is now assigned to this PhysicsStep
			AsyncInput->FinalizePendingDataStore_External(PhysicsStep);
		
			// Sync Networked InputCmds
			NetSerializePlayerControllerInputCmds(PhysicsStep, [&](FNetworkPredictionAsyncID ID, int32 PendingInputBufferFrame, FArchive& Ar)
			{
				FGlobalInstanceData& Data = GlobalInstanceData_External.FindChecked(ID);
				ForEachSim<INetSerializeService>(Data, NetSerializeServices, [&](INetSerializeService* Service)
				{
					Service->NetSerializeInputCmd(ID, PendingInputBufferFrame, Ar);
					if (Ar.IsSaving())
					{
						bOutSendClientInputCmd = true;
					}
				});
			});

			// Marshal data from GT to PT input
			for (TUniquePtr<IAsyncMarshalService_External>& ServicePtr : MarshalServices.Array)
			{
				if (IAsyncMarshalService_External* Service = ServicePtr.Get())
				{
					Service->MarshalInput(PhysicsStep, AsyncInput);
				}
			}

			PhysicsStep++;
		}

		NextPhysicsStep = PhysicsStep;

		// Consume all output data. Note we are consuming all SimCallback Output objects here even though we really only want the latest -
		// but there maybe objects that were destroyed and not present in the final output, so this is trying to not miss them.
		while (Chaos::TSimCallbackOutputHandle<FNetworkPredictionSimCallbackOutput> AsyncOutput = AsyncCallback->PopFutureOutputData_External())
		{
			GlobalOutput_External.SimulationFrame = AsyncOutput->SimulationFrame;
			GlobalOutput_External.LocalFrameOffset = AsyncOutput->LocalFrameOffset;

			for (TUniquePtr<IAsyncMarshalService_External>& ServicePtr : MarshalServices.Array)
			{
				if (IAsyncMarshalService_External* Service = ServicePtr.Get())
				{
					Service->MarshalOutput(AsyncOutput.Get());
				}
			}
		}
	}

	int32 TriggerRewindIfNeeded_Internal(int32 LastCompletedStep) override
	{
		return AsyncCallback->Reconcile_Internal(LastCompletedStep);
	}

	void PreResimStep_Internal(int32 PhysicsStep, bool bFirst) override
	{
	}

	// Returns latest simulation frame number that has been marshaled back from PT
	int32 GetLatestFrame() const
	{
		return GlobalOutput_External.SimulationFrame;
	}

private:

	struct FGlobalInstanceData
	{
		uint32 Sims;	// Bitmask of sims we are subscribed to (1 << ModelDef::GetID())
		uint8 Flags;	// EInstanceFlags
		// ENetworkPredictionAsyncInputSource InputSource = ENetworkPredictionAsyncInputSource::None; ???
	};

	template<typename InterfaceType>
	void ForEachSim(FGlobalInstanceData& InstanceData, TAsyncServiceStorage<InterfaceType>& Storage, TFunctionRef<void(InterfaceType*)> Func)
	{
		uint32 SimMask = InstanceData.Sims;
		int32 SimID=0;
		while(SimMask > 0)
		{
			if (SimMask & 1)
			{
				if (ensure(Storage.Array.IsValidIndex(SimID)))
				{
					InterfaceType* Service = Storage.Array[SimID].Get();
					if (npEnsure(Service))
					{
						Func(Service);
					}
				}
			}
			SimMask >>= 1;
			SimID++;
		}
	}

	template<typename AsyncModelDef>
	void InstantiateServicesForModelDef()
	{
		// InitializedModelDefsMask
		if ((InitializedModelDefsMask & (1 << AsyncModelDef::ID)) == 0)
		{
			InitializedModelDefsMask |= (1 << AsyncModelDef::ID);
			
			npCheckSlow(AsyncCallback);
			AsyncCallback->InstantiateServicesForModelDef<AsyncModelDef>();

			TAsyncModelDataStore_External<AsyncModelDef>* DataStore = GetDataStore<AsyncModelDef>();
			TAsyncModelDataStore_Internal<AsyncModelDef>* DataStore_Internal = AsyncCallback->GetDataStore<AsyncModelDef>();

			RegistrationServices.Instantiate<AsyncModelDef, TAsyncRegistrationService<AsyncModelDef>>(DataStore);
			MarshalServices.Instantiate<AsyncModelDef, TAsyncMarshalService_External<AsyncModelDef>>(DataStore, DataStore_Internal);
			NetSerializeServices.Instantiate<AsyncModelDef, TNetSerializeService<AsyncModelDef>>(DataStore);
		}
	}

	template<typename AsyncModelDef>
	TAsyncModelDataStore_External<AsyncModelDef>* GetDataStore()
	{
		return DataStoreCollection.GetDataStore<TAsyncModelDataStore_External<AsyncModelDef>, AsyncModelDef>();
	}

	template<typename AsyncModelDef>
	TAsyncModelDataStore_Internal<AsyncModelDef>* GetDataStore_Internal()
	{
		npCheckSlow(AsyncCallback);
		return AsyncCallback->GetDataStore<TAsyncModelDataStore_Internal<AsyncModelDef>, AsyncModelDef>();
	}

	bool IsServer() const;
	void NetSerializePlayerControllerInputCmds(int32 PhysicsStep, TFunctionRef<void(FNetworkPredictionAsyncID, int32, FArchive&)> NetSerializeFunc);

	UPROPERTY()
	UWorld* World = nullptr;
	FNetworkPredictionAsyncSimCallback* AsyncCallback = nullptr;

	// -------------------------------------------------------------------------------------
	//	Global data, shared by all the AsyncModelDefs
	// -------------------------------------------------------------------------------------

	int32 SpawnIDCounter = 1;
	int32 ClientSpawnIDCounter = -1;
	int32 NextPhysicsStep = 0;

	uint32 InitializedModelDefsMask = 0;

	TSortedMap<FNetworkPredictionAsyncID, FGlobalInstanceData> GlobalInstanceData_External;
	struct FGlobalOutput
	{
		int32 SimulationFrame = 0;
		int32 LocalFrameOffset = 0;
	};

	FGlobalOutput GlobalOutput_External;

	struct FControllerInfo
	{
		FNetworkPredictionAsyncID ID;
		APlayerController* PC = nullptr;
		ENetworkPredictionAsyncInputSource InputSource = ENetworkPredictionAsyncInputSource::None;
		int32 LastFrame = 0; // Last frame server processed (de-netserialize into datastore PendingInputCmdBuffer)

		bool operator==(const FNetworkPredictionAsyncID& id) const { return ID == id; }
	};

	TArray<FControllerInfo> PlayerControllerMap;

	// -------------------------------------------------------------------------------------
	//	AsyncModelDef specific data and services
	// -------------------------------------------------------------------------------------
	FAsyncDataStoreCollection DataStoreCollection;

	TAsyncServiceStorage<IAsyncRegistrationService> RegistrationServices;
	TAsyncServiceStorage<IAsyncMarshalService_External> MarshalServices;
	TAsyncServiceStorage<INetSerializeService> NetSerializeServices;
};

}; // namespace UE_NP
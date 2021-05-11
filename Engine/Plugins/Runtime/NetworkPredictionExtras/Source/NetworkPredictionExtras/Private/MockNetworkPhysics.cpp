// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Mock NetworkPhysics major code flow

FMockPhysInputCmd
* This is the data the client is authoratative over

[Client]
1. Gameplay code should write to UNetworkPhysicsComponent::InManagedState.InputCmd based on (engine) local input events/state.
2. The latest InputCmd (InManagedState.InputCmd) is marshalled to PT in FMockObjectManager::PreNetSend
	FIXME: it would be better if we did this via a callback from ProcessInputs_External (we want to do this one time per PT tick)
3. [To process the InputCmd] On PT, in FMockAsyncObjectManagerCallback::OnPreSimulate_Internal, the marshalled data is used in the call to AsyncTick
	NOTE: this functions look for an existing PT instance of the managed objects, if it exist, it uses the PT's PT_State instead of what was marshalled from GT
4. [To get InputCmd to server] FMockAsyncObjectManagerInput::NetSendInputCmd is called by FNetworkPhysicsRewindCallback::ProcessInputs_External
	This is saying "write the InputCmd for the first locally controlled object to the given Archive and return true"
	
[Server]
1. Receives the ServerRecvClientInputFrame RPC. This shoves the generic networked bits into a buffer on the PC
2. In FNetworkPhysicsRewindCallback::ProcessInputs_External the client pulls 1 InputCmd from the buffer
	NOTE: this is where the input buffering logic is. We want to pull 1 InputCmd per PT tick, but Cmds will arrive inconsistently. 
	If no new Cmd is available, this i called "starvation" and causes a "fault" where we let the more InputCmds buffer before pulling again.
	While in "fault", the last received InputCmd from the client is reused by the server. "The show must go on"
3. The chosen InputCmd bits are then sent to FMockAsyncObjectManagerInput::NetRecvInputCmd where its written to the ManagedObject on the SimCallbackInput
	This is saying "the object associated with this PC should NetSerialize this data into its InputCmd"
	FIXME: during starvation we end up continually re-NetSerializing the same bits on top of new inputs.
4. FMockAsyncObjectManagerCallback::OnPreSimulate_Internal will record these inputs and marshall it back to the GT
5. In FMockObjectManager::PreNetSend, the marshalled data in pushed to both ReplicatedMockManagedStates and OutMockManagedStates
	FIXME: we should try to fast track the writing of ReplicatedMockManagedState to not bounce of the PT (similiar to GT_State)
	But this complicates things if we have "early" GT_State and InputCmd but we can't know the PT_State until it is recorded on PT and sent back


FMockState_GT
* This is the data the server is authoratative over that CANNOT be written to by the PT
* (However right now nothing prevents the PT from writing to it. It will still be marshalled back but will not persist on the PT)

[Server]
1. UNetworkPhysicsComponent::InManagedState.GT_State is marshalled in PT in FMockObjectManager::PreNetSend
	FIXME: it would be better if we did this via a callback from ProcessInputs_External (we want to do this one time per PT tick)
2. On PT, in FMockAsyncObjectManagerCallback::OnPreSimulate_Internal, the marshalled data is used in the call to AsyncTick
3. In that same function, the data is marshalled back to GT via FMockAsyncObjectManagerCallback::DataFromPhysics
3. In FMockObjectManager::PreNetSend, the marshalled data in pushed to both ReplicatedMockManagedStates and OutMockManagedStates

NOTE, this is not ideal:
-We in fact are marhsalling the GT_State back to the GT
-Ideally we would fast track the GT_State to network when a write happens on the GT rather than making it do a round trip
-There may still be value in recording "what was the GT_State when this physics thread ran". I am not totally sure.

[Client]
1. Data is received via replicated property UNetworkPhysicsComponent::ReplicatedManagedState.GT_State
2. In FMockObjectManager::PostNetRecv, client looks for newly received data and marshalls it to PT
	NOTE: It actually marshalls it in two ways: 
		1. FMockAsyncObjectManagerCallback::DataFromNetwork for reconcilliation
		2. By writing it to InManagedState.GT_State so that it will be used in *new* frames
3. FMockAsyncObjectManagerCallback::TriggerRewindIfNeeded_Internal checks the Marshalled data against the locally predicted inputs of that frame
		If there is a mismatch, this will cause a correction:
		The correction is applied in FMockAsyncObjectManagerCallback::ApplyCorrections
			NOTE: we need to apply this to ALL Inputs, not just the one that it occured on. Since "latest" GT State does not automatically "carry through"
4. For new predictive frames, FMockAsyncObjectManagerCallback::OnPreSimulate_Internal will marshall this data to PT the same way the server does


FMockState_PT
* This is the data the server is authoratative over and that CAN be written to by the PT.
* The GT tells the PT the initial value. Once the object is managed on the PT, the PT state is authoratative.

1. UNetworkPhysicsComponent::InManagedState.PT_State is marshalled in PT in FMockObjectManager::PreNetSend
	FIXME: it would be better if we did this via a callback from ProcessInputs_External (we want to do this one time per PT tick)
2. On PT, in FMockAsyncObjectManagerCallback::OnPreSimulate_Internal, the marshalled data is consumed:
	If this is a "new" instance to the PT, then the entire marshalled state is accepted
	If there is already an existing instance on the PT (matched on Physics Proxy), then the PT's PT_State is reused and the marshalled PT_state is ignored.
3. AsyncTick is called on the PT's managed instance of the object. AsyncTick is allowed to modify PT_State.
4. The state prior to AsyncTick is recorded and marshalled via FMockAsyncObjectManagerCallback::DataFromPhysics
5. In FMockObjectManager::PreNetSend, the marshalled data in pushed to both ReplicatedMockManagedStates and OutMockManagedStates

[Client]
1. Data is received via replicated property UNetworkPhysicsComponent::ReplicatedManagedState.PT_State
2. In FMockObjectManager::PostNetRecv, client looks for newly received data and marshalls it to PT
	NOTE: It actually marshalls it in two ways: 
		1. FMockAsyncObjectManagerCallback::DataFromNetwork for reconcilliation
		2. By writing it to InManagedState.PT_State so that it *can* be used in *new* frames (but this will only happen if its newly spawned obj that hasn't been marshalled before).
3. FMockAsyncObjectManagerCallback::TriggerRewindIfNeeded_Internal checks the Marshalled data against the locally predicted inputs of that frame
		If there is a mismatch, this will cause a correction:
		The correction is applied in FMockAsyncObjectManagerCallback::ApplyCorrections
			NOTE: Unlike the GT_State, we only need to apply the PT_State correction on the frame it happened since PT_State "persists" across the PT frames.
4. For new predictive frames, FMockAsyncObjectManagerCallback::OnPreSimulate_Internal will marshall this data to PT the same way the server does.

=============================================================================*/

#include "MockNetworkPhysics.h"
#include "Engine/World.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"
#include "RewindData.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Chaos/SimCallbackObject.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/PlayerController.h"
#include "DrawDebugHelpers.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

// ==================================================

namespace UE_NETWORK_PHYSICS
{
	bool bFutureInputs=true;
	FAutoConsoleVariableRef CVarFutureInputs(TEXT("np2.FutureInputs"), bFutureInputs, TEXT("Enable FutureInputs feature"));

	// not convinced this is actually worth doing yet, so leaving it off. Adds too much noise to tuning the lower level stuff
	bool bInputDecay=false;
	FAutoConsoleVariableRef CVarInputDecay(TEXT("np2.InputDecay"), bInputDecay, TEXT("Enable Input Decay Feature"));

	float InputDecayRate=0.99f;
	FAutoConsoleVariableRef CVarInputDecayRate(TEXT("np2.InputDecayRate"), InputDecayRate, TEXT("Rate of input decay"));

	bool MockDebug=false;
	FAutoConsoleVariableRef CVarMockDebug(TEXT("np2.Mock.Debug"), MockDebug, TEXT("Enabled spammy log debugging of mock physics object state"));
}

void FMockManagedState::AsyncTick(UWorld* World, Chaos::FPhysicsSolver* Solver, const float DeltaSeconds, const int32 SimulationFrame, const int32 LocalStorageFrame)
{
	// FIXME: PT_State is the only thing that should be writable here
	// FIXME: DeltaSeconds/TotalSecond are local time and therefor worthless. This needs to be server frame/time

	if (ensure(Proxy))
	{
		if (auto* PT = Proxy->GetPhysicsThreadAPI())
		{
			if (InputCmd.bBrakesPressed)
			{
				PT->SetV( Chaos::FVec3(0.f));				
				PT->SetW( Chaos::FVec3(0.f));

				UE_CLOG(UE_NETWORK_PHYSICS::MockDebug, LogNetworkPhysics, Log, TEXT("[%d/%d] Applied Break. Rot was: %s"), SimulationFrame, LocalStorageFrame, *PT->R().Rotator().ToString());
				Chaos::FRotation3 NewR = PT->R();
				FRotator Rot = NewR.Rotator();
				Rot.Pitch = 0.f;
				Rot.Roll = 0.f;
				PT->SetR( FQuat(Rot));
			}
			else
			{
				if (InputCmd.Force.SizeSquared() > 0.001f)
				{
					//UE_LOG(LogTemp, Warning, TEXT("Applied Force. Frame: %d"), LocalFrame);
					//UE_LOG(LogTemp, Warning, TEXT("0x%X ForceMultiplier: %f (Rand: %d)"), (int64)Proxy, GT_State.ForceMultiplier, GT_State.RandValue);
					//UE_CLOG(PC == nullptr && InputCmd.Force.SizeSquared() > 0.f, LogTemp, Warning, TEXT("0x%X ForceMultiplier: %f (Rand: %d)"), (int64)Proxy, GT_State.ForceMultiplier, GT_State.RandValue);
					PT->AddForce(InputCmd.Force * GT_State.ForceMultiplier);
				}
				
				if (FMath::Abs<float>(InputCmd.Turn) > 0.001f)
				{
					PT->AddTorque(Chaos::FVec3(0.0f, 0.0f, InputCmd.Turn * GT_State.ForceMultiplier * 10.f));
				}				
			}

			if (InputCmd.bJumpedPressed)
			{
				PT_State.JumpCount++;
				UE_CLOG(UE_NETWORK_PHYSICS::MockDebug, LogNetworkPhysics, Log, TEXT("[%d/%d] bJumpedPressed: %d. Count: %d"), SimulationFrame, LocalStorageFrame, InputCmd.bJumpedPressed, PT_State.JumpCount);
			}

			//UE_CLOG(PC == nullptr, LogTemp, Warning, TEXT("NP [%d] bJumpedPressed: %d. Count: %d"), LocalFrame, InputCmd.bJumpedPressed, PT_State.JumpCount);
			if (InputCmd.bJumpedPressed && PT_State.JumpCooldownMS == 0)
			{
				//UE_LOG(LogTemp, Warning, TEXT("0x%X Jump! Total: %d"), (int64)Proxy, (int32)(TotalSeconds*1000.f));
				PT->SetLinearImpulse( Chaos::FVec3(0.f, 0.f, 115000.f) );
				PT_State.JumpCooldownMS = 1000;
				UE_CLOG(UE_NETWORK_PHYSICS::MockDebug, LogNetworkPhysics, Log, TEXT("[%d/%d] Applied Jump and reset cooldown"), SimulationFrame, LocalStorageFrame);
			}
			else
			{
				PT_State.JumpCooldownMS = FMath::Max( PT_State.JumpCooldownMS - (int32)(DeltaSeconds* 1000.f), 0);
				if (PT_State.JumpCooldownMS != 0)
				{
					UE_CLOG(UE_NETWORK_PHYSICS::MockDebug, LogNetworkPhysics, Log, TEXT("[%d/%d] JumpCount: %d. JumpCooldown: %d"), SimulationFrame, LocalStorageFrame, PT_State.JumpCount, PT_State.JumpCooldownMS);
				}
			}
		}
	}
}

// ==================================================

struct FMockAsyncObjectManagerInput : public Chaos::FSimCallbackInput
{
	// One per instance of our physics objects
	TArray<FMockManagedState> ManagedObjects;
	
	TWeakObjectPtr<UWorld> World;
	int32 Timestamp = INDEX_NONE;

	void Reset()
	{
		ManagedObjects.Reset();
		World.Reset();
	}

	bool NetSendInputCmd(FNetBitWriter& Ar) override
	{
		bool bNetSuccess = true;
		for (FMockManagedState& Obj: ManagedObjects)
		{
			// Only called on client, find first one with a valid PC 
			// (but maybe we should pass the PC in just to be safe)
			if (Obj.PC)
			{
				Obj.InputCmd.NetSerialize(Ar, nullptr, bNetSuccess);
				return true;
			}			
		}
		return false; 
	}

	bool NetRecvInputCmd(APlayerController* PC, FNetBitReader& Ar) override
	{
		bool bNetSuccess = true;
		for (FMockManagedState& Obj: ManagedObjects)
		{
			if (Obj.PC == PC)
			{
				Obj.InputCmd.NetSerialize(Ar, nullptr, bNetSuccess);
				return true;
			}			
		}
		return false; 
	}
};

struct FMockAsyncObjectManagerOutput : public Chaos::FSimCallbackOutput
{
	// No object state to deal with for now
	void Reset()
	{

	}
};

class FMockAsyncObjectManagerCallback : public Chaos::TSimCallbackObject<FMockAsyncObjectManagerInput, FMockAsyncObjectManagerOutput>
{
public:

	virtual void OnPreSimulate_Internal() override
	{
		const FMockAsyncObjectManagerInput* Input = GetConsumerInput_Internal();
		if (Input == nullptr)
		{
			return;
		}

		UWorld* World = Input->World.Get();
		Chaos::FPhysicsSolver* PhysicsSolver = static_cast<Chaos::FPhysicsSolver*>(GetSolver());

		// Record Inputs for future reconciles
		const int32 Frame = PhysicsSolver->GetCurrentFrame();
		//UE_CLOG(LastRecordedInputFrame != INDEX_NONE && (Frame != LastRecordedInputFrame+1), LogNetworkPhysics, Warning, TEXT("Gap in recorded input frame? Last: %d. Now: %d"), LastRecordedInputFrame, Frame);

		RecordedInputs[Frame % RecordedInputs.Num()] = Input;
		LastRecordedInputFrame = Frame;
		const int32 SimulationFrame = Frame - this->LocalFrameOffset;

		// Run AsyncTick for all objects we are managing
		TArray<FMockManagedState> Old_PT_ManagedObjects = MoveTemp(PT_ManagedObjects);
		PT_ManagedObjects.Reset();

		FSnapshot SnapshotForGT;

		for (int32 idx=0; idx < Input->ManagedObjects.Num(); ++idx)
		{
			FMockManagedState& Obj = const_cast<FMockManagedState&>(Input->ManagedObjects[idx]);

			// FIXME
			// Search for existing PT managed state. If we find one, we use its PT_State instead of what was mashalled.
			// (We will eventually provide a lambda for doing GT -> PT writes to this state. For now, not possible)
			FMockManagedState& PT_Obj = PT_ManagedObjects.Emplace_GetRef(Obj);

			if (bNextStepIsResim == false)
			{
				for (FMockManagedState& ExistingState : Old_PT_ManagedObjects)
				{
					if (ExistingState.Proxy == Obj.Proxy)
					{
						PT_Obj.PT_State = ExistingState.PT_State;
						Obj.PT_State = ExistingState.PT_State;	// FIXME: We have to copy the official PT_State back into the Input storage too, for corrections
						break;
					}
				}
			}
			else
			{
				// On first step of resim, we need to use the input state, not the "latest" in PT_ManagedObjects
				PT_Obj.PT_State = Obj.PT_State;
			}

			// Marshall data back to GT for networking and gameplay code view
			// Note that is is important that we do this before the call to AsyncTick (we are making a copy for SnapshotForGT)
			//	-We are saying "these were the inputs used for this physics tick"
			//	-If we marshalled the output, it would be for frame+1
			//	(we could potentially look at this in order to get fresher data to client faster, but it complicates the implementation)
			PT_Obj.Frame = Frame;
			SnapshotForGT.Objects.Add(PT_Obj);
			
			PT_Obj.AsyncTick(World, PhysicsSolver, GetDeltaTime_Internal(), SimulationFrame, Frame);
		}

		if (SnapshotForGT.Objects.Num() > 0)
		{
			DataFromPhysics.Enqueue(SnapshotForGT);
		}

		bNextStepIsResim = false;
	}

	virtual void OnContactModification_Internal(const TArrayView<Chaos::FPBDCollisionConstraintHandleModification>& Modifications) override
	{

	}

	int32 TriggerRewindIfNeeded_Internal(int32 LastCompletedStep)
	{
		PendingCorrections.Reset();

		FSnapshot Snapshot;
		int32 RewindToFrame = INDEX_NONE;

		while (DataFromNetwork.Dequeue(Snapshot))
		{
			this->LocalFrameOffset = Snapshot.LocalFrameOffset;
			for (FMockManagedState& Obj : Snapshot.Objects)
			{
				if (Obj.Frame <= LastRecordedInputFrame - RecordedInputs.Num())
				{
					// Too old to reconcile
					UE_LOG(LogNetworkPhysics, Warning, TEXT("State too old to reconcile. %d. Latest: %d"), Obj.Frame, LastRecordedInputFrame);
					continue;
				}

				const FMockAsyncObjectManagerInput* LocalInput = RecordedInputs[Obj.Frame % RecordedInputs.Num()];
				if (!LocalInput)
				{
					continue;
				}

				// FIXME: linear search for object to compare against
				for (const FMockManagedState& LocalState : LocalInput->ManagedObjects)
				{
					if (Obj.Proxy != LocalState.Proxy)
						continue;

					// SP needs to reconcile InputCmd see notes in FMockPhysInputCmd::ShouldReconcile
					bool bInputReconcile = false;
					if (Obj.PC == nullptr && LocalState.InputCmd.ShouldReconcile(Obj.InputCmd))
					{
						bInputReconcile = true;
						UE_CLOG(UE_NETWORK_PHYSICS::MockDebug, LogNetworkPhysics, Warning, TEXT("INPUT reconcile. Force: %s   %s. Delta: %s. Equal: %d"), *Obj.InputCmd.Force.ToString(), *LocalState.InputCmd.Force.ToString(), *(Obj.InputCmd.Force - LocalState.InputCmd.Force).ToString(), (Obj.InputCmd.Force == LocalState.InputCmd.Force ? 1 : 0) );
					}

					if (LocalState.GT_State.ShouldReconcile(Obj.GT_State) || LocalState.PT_State.ShouldReconcile(Obj.PT_State) || bInputReconcile)
					{
						//UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections, LogNetworkPhysics, Log, TEXT("Rewind Needed for MockPersistState. Obj.Frame: %d. LastCompletedStep: %d."), Obj.Frame, LastCompletedStep);
						UE_CLOG(UE_NETWORK_PHYSICS::MockDebug, LogNetworkPhysics, Error, TEXT("[%d] Rewind Needed for MockPersistState. Obj.Frame: %d. LastCompletedStep: %d. JumpCnt: %d (auth) vs %d (pred)"), Obj.Frame - this->LocalFrameOffset, Obj.Frame, LastCompletedStep, Obj.PT_State.JumpCount, LocalState.PT_State.JumpCount);
 						UE_CLOG(UE_NETWORK_PHYSICS::MockDebug, LogNetworkPhysics, Error, TEXT("     Server: JumpCnt: %d. JumpCooldownMS: %d"), Obj.PT_State.JumpCount, Obj.PT_State.JumpCooldownMS);
						UE_CLOG(UE_NETWORK_PHYSICS::MockDebug, LogNetworkPhysics, Error, TEXT("     Local:  JumpCnt: %d. JumpCooldownMS: %d"), LocalState.PT_State.JumpCount, LocalState.PT_State.JumpCooldownMS);


						RewindToFrame = RewindToFrame == INDEX_NONE ? Obj.Frame : FMath::Min(RewindToFrame, Obj.Frame);
						ensure(RewindToFrame >= 0);

						PendingCorrections.Emplace(Obj);
					}
				}
			}
		}

		return RewindToFrame; 
	}
	
	void ApplyCorrections_Internal(int32 PhysicsStep, Chaos::FSimCallbackInput* Input) override
	{
		// Note: this can't work like FNetworkPhysicsRewindCallback::PreResimStep_Internal because even after we apply
		// a correction on the frame it occured on, we still need to apply GT data to all subsequent frames. This seems
		// bad and it would be better if we had a better way of storing the GT data on the PT? 

		FMockAsyncObjectManagerInput* AsyncInput = (FMockAsyncObjectManagerInput*)Input;
		for (int32 idx=0; idx < PendingCorrections.Num(); ++idx)
		{
			FMockManagedState& CorrectionState = PendingCorrections[idx];
			if (CorrectionState.Frame > PhysicsStep)
			{
				// Correction hasn't happened yet
				continue;
			}

			// Fixme: terrible for loop
			for (FMockManagedState& InputState : AsyncInput->ManagedObjects)
			{
				if (InputState.Proxy == CorrectionState.Proxy)
				{
					UE_LOG(LogNetworkPhysics, Log, TEXT("Applying Mock Object Correction from frame %d (actual step: %d)"), CorrectionState.Frame, PhysicsStep);
					if (CorrectionState.Frame == PhysicsStep)
					{
						// Correction happened on this frame, this is the only frame we override the PT data
						// But oh god we have to find the PT_ManagedObject if it exists, because that will be the actual state that is used in async tick
						UE_LOG(LogNetworkPhysics, Log, TEXT("Applying correction for PT_state. %d -> %d. JumpCnt: %d -> %d"), InputState.PT_State.JumpCooldownMS, CorrectionState.PT_State.JumpCooldownMS, InputState.PT_State.JumpCount, CorrectionState.PT_State.JumpCount);
						for (FMockManagedState& PT_Obj : PT_ManagedObjects)
						{
							if (PT_Obj.Proxy == CorrectionState.Proxy)
							{
								PT_Obj = CorrectionState;
								break;
							}
						}

						InputState.PT_State = CorrectionState.PT_State;
					}
					
					// GT data has to be overridden each time
					InputState.GT_State = CorrectionState.GT_State;

					if (InputState.PC == nullptr)
					{
						// SP has to take InputCmd too
						// (This would cause AP to lose their actual inputcmds which would cause a spiral of corrections)

						if (UE_NETWORK_PHYSICS::bFutureInputs)
						{
							if (CorrectionState.Frame == PhysicsStep)
							{
								InputState.InputCmd = CorrectionState.InputCmd;
							}
							else
							{
								bool bFound = false;
								for (FMockFutureClientInput& Future : CorrectionState.FutureInputs)
								{
									if (Future.Frame == PhysicsStep)
									{
										//UE_LOG(LogNetworkPhysics, Warning, TEXT("Using Future frame in correction!"));
										InputState.InputCmd = Future.InputCmd;
										bFound = true;
										break;
									}
								}
								if (!bFound)
								{
									InputState.InputCmd = CorrectionState.FutureInputs.Num() > 0 ? CorrectionState.FutureInputs.Last().InputCmd : CorrectionState.InputCmd;
								}
							}
						}
						else
						{
							InputState.InputCmd = CorrectionState.InputCmd;
						}
						

					}

					break;
				}
			}
		}
	}

	void FirstPreResimStep_Internal(int32 PhysicsStep) override
	{
		bNextStepIsResim = true;
	}

	// PT Copies of what we are managing
	TArray<FMockManagedState> PT_ManagedObjects;

	struct FSnapshot
	{
		int32 LocalFrameOffset = 0;
		TArray<FMockManagedState> Objects;
	};

	TQueue<FSnapshot> DataFromNetwork;	// Data used to check for corrections
	TQueue<FSnapshot> DataFromPhysics;	// Data sent back to GT for networking

	int32 LocalFrameOffset = 0; // Latest mapping of local->server frame numbers. Updated via FSnapshot::LocalFrameOffset
	
	int32 LastRecordedInputFrame = INDEX_NONE;
	TStaticArray<const FMockAsyncObjectManagerInput*, 64> RecordedInputs;

	TArray<FMockManagedState> PendingCorrections;
	bool bNextStepIsResim = false;
};

// ----------------------------------------------------------------------------------

FMockObjectManager* FMockObjectManager::Get(UWorld* World)
{
	if (UNetworkPhysicsManager* NetworkPhysicsManager = World->GetSubsystem<UNetworkPhysicsManager>())
	{
		if (FMockObjectManager* Existing = NetworkPhysicsManager->GetSubsystem<FMockObjectManager>())
		{
			return Existing;
		}

		FMockObjectManager* NewInstance = NetworkPhysicsManager->RegisterSubsystem<FMockObjectManager>(MakeUnique<FMockObjectManager>(World));
		return NewInstance;
	}
	ensure(false);
	return nullptr;
}

FMockObjectManager::FMockObjectManager(UWorld* World)
{
	if (ensure(World))
	{
		FPhysScene* PhysScene = World->GetPhysicsScene();
		if (ensureAlways(PhysScene))
		{
			Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();
			if (ensureAlways(Solver))
			{
				if (Solver->GetRewindCallback())
				{
					AsyncCallback = Solver->CreateAndRegisterSimCallbackObject_External<FMockAsyncObjectManagerCallback>(true, true);
				}
				else
				{
					UE_LOG(LogNetworkPhysics, Warning, TEXT("Rewind not enabled on Physics solver. FMockObjectManager will be disabled"));
				}
			}
		}
	}
}

FMockObjectManager::~FMockObjectManager()
{

}

void FMockObjectManager::RegisterManagedMockObject(FMockManagedState* ReplicatedState, FMockManagedState* InState, FMockManagedState* OutState)
{
	ensure(ReplicatedMockManagedStates.Contains(ReplicatedState) == false);
	ensure(InMockManagedStates.Contains(InState) == false);
	ensure(OutMockManagedStates.Contains(OutState) == false);
	ReplicatedMockManagedStates.Add(ReplicatedState);
	InMockManagedStates.Add(InState);
	OutMockManagedStates.Add(OutState);
}

void FMockObjectManager::UnregisterManagedMockObject(FMockManagedState* ReplicatedState, FMockManagedState* InState, FMockManagedState* OutState)
{
	ensure(ReplicatedMockManagedStates.RemoveSingleSwap(ReplicatedState, false) == 1);
	ensure(InMockManagedStates.RemoveSingleSwap(InState, false) == 1);
	ensure(OutMockManagedStates.RemoveSingleSwap(OutState, false) == 1);
}

void FMockObjectManager::PostNetRecv(UWorld* World, int32 FrameOffset, int32 LastProcessedFrame)
{
	const bool bIsServer = World->GetNetMode() != NM_Client;

	if (bIsServer)
	{

	}
	else
	{
		// Client: marshal data from network for reconciliation
		FMockAsyncObjectManagerCallback::FSnapshot Snapshot;
		Snapshot.LocalFrameOffset = FrameOffset;

		for (FMockManagedState* ReplicatedMockState : ReplicatedMockManagedStates)
		{
			if (ReplicatedMockState->Frame > LastProcessedFrame)
			{
				const int32 LocalFrame = ReplicatedMockState->Frame + FrameOffset;
				if (LocalFrame > 0)
				{
					// Marshal a copy of the new data to PT for reconciliation
					FMockManagedState& MarshalledCopy = Snapshot.Objects.Emplace_GetRef(*ReplicatedMockState);
					
					UE_CLOG(UE_NETWORK_PHYSICS::MockDebug, LogNetworkPhysics, Log, TEXT("[%d/%d] %d.  Client NetRecv Marhsal->PT. JumpCnt: %d JumpCooldown: %d"), MarshalledCopy.Frame, LocalFrame, FrameOffset, MarshalledCopy.PT_State.JumpCount, MarshalledCopy.PT_State.JumpCooldownMS);						
					MarshalledCopy.Frame = LocalFrame;

					// Convert server->local frame number for future inputs
					for (FMockFutureClientInput& Future : MarshalledCopy.FutureInputs)
					{
						Future.Frame += FrameOffset;
					}
				}

				// GT State should immediately be written to InManagedState so that it is used for new frames
				// (This would inhibit predictive GT state writes by client. If we want to support that we will need to store those and reinject)
				for (FMockManagedState* LocalInMockState : InMockManagedStates)
				{
					if (LocalInMockState->Proxy == ReplicatedMockState->Proxy)
					{
						LocalInMockState->GT_State = ReplicatedMockState->GT_State;
						LocalInMockState->PT_State = ReplicatedMockState->PT_State; // This is required to get the correct initial state over to PT (in case where server changes PT state on the GT at spawn)
						
						if (LocalInMockState->PC == nullptr)
						{
							// Only copy the InputCmd over if not locally controlled. Otherwise we may be overriting gameplay code's submitted input cmd
							if (ReplicatedMockState->FutureInputs.Num() > 0)
							{
								// Use the latest server recv (but not processed at the time this was sent) for future frames
								// (note that though this was not processed server side when server sent this to us, we are already 'ahead' of this frame locally 
								// [under the assumption of relatively equal amounts of server side input buffers. A low buffered client could potentially get ahead of a high buffered client but no reason to optimize for that case)
								LocalInMockState->InputCmd = ReplicatedMockState->FutureInputs.Last().InputCmd;
							}
							else
							{
								LocalInMockState->InputCmd = ReplicatedMockState->InputCmd;
							}
						}
					}
				}
			}
		}

		if (Snapshot.Objects.Num() > 0)
		{
			AsyncCallback->DataFromNetwork.Enqueue(MoveTemp(Snapshot));
		}
	}
}

void FMockObjectManager::PreNetSend(UWorld* World, float DeltaSeconds)
{
	if (!ensure(AsyncCallback))
	{
		return;
	}

	// -------------------------------------------
	// Marshall the managed objects to PT
	// This part needs to happen once per PT tick - currently being called in PreNetSend (probably) every frame on GT is not ideal
	// -------------------------------------------
	
	FMockAsyncObjectManagerInput* AsyncInput = AsyncCallback->GetProducerInputData_External();
	AsyncInput->Reset();	//only want latest frame's data

	AsyncInput->ManagedObjects.Reserve(InMockManagedStates.Num());
		
	for (FMockManagedState* State : InMockManagedStates)
	{
		// Should we decay the GT InputCmd or just the marshalled copy?
		// Probably want non linear decay?
		if (UE_NETWORK_PHYSICS::bInputDecay && State->PC == nullptr)
		{
			if (State->InputDecay > 0.f)
			{
				State->InputCmd.Decay(State->InputDecay);
			}
			State->InputDecay += DeltaSeconds * UE_NETWORK_PHYSICS::InputDecayRate;
		}

		AsyncInput->ManagedObjects.Emplace( *State );
	}

	// -------------------------------------------
	//	Marshall data from PT
	// -------------------------------------------

	const bool bIsServer = World->GetNetMode() != NM_Client;

	FMockAsyncObjectManagerCallback::FSnapshot Snapshot;
	bool bFoundData = false;
	while (AsyncCallback->DataFromPhysics.Dequeue(Snapshot))
	{
		bFoundData = true;
	}

	if (bFoundData)
	{
		bool bOutSuccess = true;
		for (FMockManagedState& PTState : Snapshot.Objects)
		{
			if (bIsServer)
			{
				// Only the server should marshal data to the Replicated states
				for (FMockManagedState* ManagedMockState : ReplicatedMockManagedStates)
				{
					// This part is sketchy, see notes in UNetworkPhysicsManager::PreNetSend
					// Will probably need some per-system ID and a map to do this lookup
					if (ManagedMockState->Proxy == PTState.Proxy)
					{
						ManagedMockState->InputCmd = PTState.InputCmd;
						ManagedMockState->PT_State = PTState.PT_State;
						ManagedMockState->GT_State = PTState.GT_State;
						ManagedMockState->Frame = PTState.Frame;

						UE_CLOG(UE_NETWORK_PHYSICS::MockDebug, LogNetworkPhysics, Log, TEXT("[%d] Server Marhsal->GT. JumpCnt: %d JumpCooldown: %d"), PTState.Frame, PTState.PT_State.JumpCount, PTState.PT_State.JumpCooldownMS);

						// Send future inputs
						if (ManagedMockState->PC != nullptr && UE_NETWORK_PHYSICS::bFutureInputs)
						{
							// FIXME: this is pretty bad since we are now doubling up on deserialzing everything!
							APlayerController::FServerFrameInfo& FrameInfo = ManagedMockState->PC->GetServerFrameInfo();
							APlayerController::FInputCmdBuffer& InputBuffer = ManagedMockState->PC->GetInputBuffer();

							ManagedMockState->FutureInputs.Reset();

							if (FrameInfo.LastProcessedInputFrame > 0)
							{
								int32 FutureServerFrame = FrameInfo.LastLocalFrame;
								for (int32 FutureClientFrame = FrameInfo.LastProcessedInputFrame; FutureClientFrame <= InputBuffer.HeadFrame(); ++FutureClientFrame, ++FutureServerFrame)
								{
									const TArray<uint8>& InputCmdData = InputBuffer.Get(FutureClientFrame);
									if (InputCmdData.Num() > 0)
									{
										uint8* RawData = const_cast<uint8*>(InputCmdData.GetData());
										FNetBitReader Ar(nullptr, RawData, ((int64)InputCmdData.Num()) << 3);

										FMockFutureClientInput& ReplicatedFutureInput = ManagedMockState->FutureInputs.AddDefaulted_GetRef();
										ReplicatedFutureInput.Frame = FutureServerFrame;
										ReplicatedFutureInput.InputCmd.NetSerialize(Ar, nullptr, bOutSuccess);
									}
								}
							}
						}
					}
				}
			}

			// Both client and server marshall PT state to the Output State
			for (FMockManagedState* ManagedMockState : OutMockManagedStates)
			{
				// This part is sketchy, see notes in UNetworkPhysicsManager::PreNetSend
				// Will probably need some per-system ID and a map to do this lookup
				if (ManagedMockState->Proxy == PTState.Proxy)
				{
					ManagedMockState->InputCmd = PTState.InputCmd;
					ManagedMockState->PT_State = PTState.PT_State;
					ManagedMockState->GT_State = PTState.GT_State;	// Note: this is saying "this was the GT state when this frame ran on the PT"
					ManagedMockState->Frame = PTState.Frame;
				}
			}
		}
	}
}

// ========================================================================================

UNetworkPhysicsComponent::UNetworkPhysicsComponent()
{
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	
}

void UNetworkPhysicsComponent::InitializeComponent()
{
#if WITH_CHAOS
	Super::InitializeComponent();

	UWorld* World = GetWorld();	
	checkSlow(World);
	UNetworkPhysicsManager* Manager = World->GetSubsystem<UNetworkPhysicsManager>();
	if (!Manager)
	{
		return;
	}
	
	UPrimitiveComponent* PrimitiveComponent = nullptr;
	if (AActor* MyActor = GetOwner())
	{
		if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(MyActor->GetRootComponent()))
		{
			PrimitiveComponent = RootPrimitive;
		}
		else if (UPrimitiveComponent* FoundPrimitive = MyActor->FindComponentByClass<UPrimitiveComponent>())
		{
			PrimitiveComponent = FoundPrimitive;
		}
	}

	if (ensureMsgf(PrimitiveComponent, TEXT("No PrimitiveComponent found on %s"), *GetPathName()))
	{
		NetworkPhysicsState.Proxy = PrimitiveComponent->BodyInstance.ActorHandle;
		Manager->RegisterPhysicsProxy(&NetworkPhysicsState);

		Manager->RegisterPhysicsProxyDebugDraw(&NetworkPhysicsState, [this](const UNetworkPhysicsManager::FDrawDebugParams& P)
		{
			AActor* Actor = this->GetOwner();
			FBox LocalSpaceBox = Actor->CalculateComponentsBoundingBoxInLocalSpace();
			const float Thickness = 2.f;

			FVector ActorOrigin;
			FVector ActorExtent;
			LocalSpaceBox.GetCenterAndExtents(ActorOrigin, ActorExtent);
			ActorExtent *= Actor->GetActorScale3D();
			DrawDebugBox(P.DrawWorld, P.Loc, ActorExtent, P.Rot, P.Color, false, P.Lifetime, 0, Thickness);
		});

		
		FMockObjectManager* MockManager = FMockObjectManager::Get(World);
		checkSlow(MockManager);

		InManagedState.Proxy = PrimitiveComponent->BodyInstance.ActorHandle;
		OutManagedState.Proxy = PrimitiveComponent->BodyInstance.ActorHandle;
		ReplicatedManagedState.Proxy = PrimitiveComponent->BodyInstance.ActorHandle;
		MockManager->RegisterManagedMockObject(&ReplicatedManagedState, &InManagedState, &OutManagedState);
	}
#endif
}

void UNetworkPhysicsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	//UE_LOG(LogTemp, Warning, TEXT("EndPlay %s"), *GetPathNameSafe(this));
	if (UWorld* World = GetWorld())
	{
		if (UNetworkPhysicsManager* Manager = World->GetSubsystem<UNetworkPhysicsManager>())
		{
			if (FMockObjectManager* MockManager = FMockObjectManager::Get(World))
			{
				//UE_LOG(LogTemp, Warning, TEXT("   Unregistering MockManager. 0x%X"), (int64)&ReplicatedManagedState);
				MockManager->UnregisterManagedMockObject(&ReplicatedManagedState, &InManagedState, &OutManagedState);				
			}
			Manager->UnregisterPhysicsProxy(&NetworkPhysicsState);
		}
	}
}

void UNetworkPhysicsComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APlayerController* PC = GetOwnerPC();
	if (PC && PC->IsLocalController())
	{
		// Broadcast out a delegate. The user will use GetPendingInputCmd/SetPendingInputCmd to write to ManagedState.InputCmd
		OnGeneratedLocalInputCmd.Broadcast();
	}

	ReplicatedManagedState.PC = PC;
	InManagedState.PC = PC;
	OutManagedState.PC = PC;
}

APlayerController* UNetworkPhysicsComponent::GetOwnerPC() const
{
	if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(PawnOwner->GetController());
	}

	return nullptr;
}

void UNetworkPhysicsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME( UNetworkPhysicsComponent, NetworkPhysicsState);
	DOREPLIFETIME( UNetworkPhysicsComponent, ReplicatedManagedState);
}


// ============================================================================================================
// ============================================================================================================
// ============================================================================================================


FAutoConsoleCommandWithWorldAndArgs ForceMockCorrectionCmd(TEXT("np2.ForceMockCorrection"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* InWorld) 
{
	for (TObjectIterator<UWorld> It; It; ++It)
	{
		UWorld* World = *It;
		if (World->GetNetMode() != NM_Client && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game))
		{
			const float NewMultiplier = 150000.f + (FMath::FRand() * 250000.f);

			for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
			{
				if (UNetworkPhysicsComponent* PhysComp = ActorIt->FindComponentByClass<UNetworkPhysicsComponent>())
				{
					FMockState_GT NewState = PhysComp->GetMockState_GT();
					NewState.ForceMultiplier = NewMultiplier;
					PhysComp->SetMockState_GT(NewState);

					UE_LOG(LogNetworkPhysics, Warning, TEXT("Setting ForceMultiplier on %s to %.2f"), *PhysComp->GetPathName(), NewMultiplier);
				}
			}
		}
	}
}));

FAutoConsoleCommandWithWorldAndArgs ForceMockCorrectionCmd2(TEXT("np2.ForceMockCorrection2"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* InWorld) 
{
	for (TObjectIterator<UWorld> It; It; ++It)
	{
		UWorld* World = *It;
		if (World->GetNetMode() != NM_Client && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game))
		{
			const int32 NewRand = FMath::RandHelper(1024);

			for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
			{
				if (UNetworkPhysicsComponent* PhysComp = ActorIt->FindComponentByClass<UNetworkPhysicsComponent>())
				{
					FMockState_GT NewState = PhysComp->GetMockState_GT();
					NewState.RandValue = NewRand;
					PhysComp->SetMockState_GT(NewState);

					UE_LOG(LogNetworkPhysics, Warning, TEXT("Setting NewRand on %s to %d"), *PhysComp->GetPathName(), NewRand);
				}
			}
		}
	}
}));
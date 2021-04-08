// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPhysics.h"
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

DEFINE_LOG_CATEGORY(LogNetworkPhysics);

namespace UE_NETWORK_PHYSICS
{
	float X = 1.0;	FAutoConsoleVariableRef CVarX(TEXT("np2.Tolerance.X"), X, TEXT("Location Tolerance"));
	float R = 0.1;	FAutoConsoleVariableRef CVarR(TEXT("np2.Tolerance.R"), R, TEXT("Rotation Tolerance"));
	float V = 1.0;	FAutoConsoleVariableRef CVarV(TEXT("np2.Tolerance.V"), V, TEXT("Velocity Tolerance"));
	float W = 1.0;	FAutoConsoleVariableRef CVarW(TEXT("np2.Tolerance.W"), W, TEXT("Rotational Velocity Tolerance"));

	bool bForceResim=false;
	FAutoConsoleVariableRef CVarResim(TEXT("np2.ForceResim"), bForceResim, TEXT("Forces near constant resimming"));

	bool bEnable=false;
	FAutoConsoleVariableRef CVarEnable(TEXT("np2.bEnable"), bEnable, TEXT("Enabled rollback physics. Must be set before starting game"));

	bool bLogCorrections=true;
	FAutoConsoleVariableRef CVarLogCorrections(TEXT("np2.LogCorrections"), bLogCorrections, TEXT("Logs corrections when they happen"));

	int32 FixedLocalFrameOffset = -1;
	FAutoConsoleVariableRef CVarFixedLocalFrameOffset(TEXT("np2.FixedLocalFrameOffset"), FixedLocalFrameOffset, TEXT("When > 0, use hardcoded frame offset on client from head"));

	int32 FixedLocalFrameOffsetTolerance = 3;
	FAutoConsoleVariableRef CVarFixedLocalFrameOffsetTolerance(TEXT("np2.FixedLocalFrameOffsetTolerance"), FixedLocalFrameOffsetTolerance, TEXT("When > 0, use hardcoded frame offset on client from head"));
}

struct FNetworkPhysicsRewindCallback : public Chaos::IRewindCallback
{
	bool CompareVec(const FVector& A, const FVector& B, const float D, const TCHAR* Str)
	{
		const bool b = FVector::DistSquared(A, B) > D * D;
		UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections && b, LogNetworkPhysics, Log, TEXT("%s correction. Local: %s. Server: %s. Delta: %s (%.4f)"), Str, *A.ToString(), *B.ToString(), *(A-B).ToString(), (A-B).Size());
		return  b;
	}
	bool CompareQuat(const FQuat& A, const FQuat& B, const float D, const TCHAR* Str)
	{
		const bool b = FQuat::ErrorAutoNormalize(A, B) > D;
		UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections && b, LogNetworkPhysics, Log, TEXT("%s correction. Local: %s. Server: %s. Delta: %f"), Str, *A.ToString(), *B.ToString(), FQuat::ErrorAutoNormalize(A, B));
		return b;
	}
	bool CompareObjState(Chaos::EObjectStateType A, Chaos::EObjectStateType B)
	{
		const bool b = A != B;
		UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections && b, LogNetworkPhysics, Log, TEXT("ObjectState correction. Local: %d. Server: %d."), A, B);
		return b;
	}

	int32 TriggerRewindIfNeeded_Internal(int32 LastCompletedStep) override
	{
		if (ResimEndFrame != INDEX_NONE)
		{
			// We are already in a resim and shouldn't trigger a new one
			return INDEX_NONE;
		}

		if (UE_NETWORK_PHYSICS::bForceResim)
		{
			DataFromNetwork.Empty();
			if (LastCompletedStep > 100 && LastCompletedStep > LastResim+4)
			{
				LastResim = LastCompletedStep;
				return LastCompletedStep - 4;
			}
			return INDEX_NONE;
		}
		
		checkSlow(RewindData);
		const int32 MinFrame = RewindData->CurrentFrame() - RewindData->GetFramesSaved();
		
		PendingCorrections.Reset();

		FSnapshot Snapshot;
		int32 RewindToFrame = INDEX_NONE;
		while (DataFromNetwork.Dequeue(Snapshot))
		{
			for (FNetworkPhysicsState& Obj : Snapshot.Objects)
			{
				FSingleParticlePhysicsProxy* Proxy = Obj.Proxy;
				check(Proxy);

				if (Obj.Frame < MinFrame)
				{
					UE_LOG(LogNetworkPhysics, Log, TEXT("Obj too stale to reconcile. Frame: %d. LastCompletedStep: %d"), Obj.Frame, LastCompletedStep);
					continue;
				}

				if (Proxy->GetHandle_LowLevel() == nullptr)
				{
					continue;
				}

				const auto P = RewindData->GetPastStateAtFrame(*Proxy->GetHandle_LowLevel(), Obj.Frame);
				
				if (CompareObjState(Obj.ObjectState, P.ObjectState()) ||
					CompareVec(Obj.Location, P.X(), UE_NETWORK_PHYSICS::X, TEXT("Location")) ||
					CompareVec(Obj.LinearVelocity, P.V(), UE_NETWORK_PHYSICS::V, TEXT("Velocity")) ||
					CompareVec(Obj.AngularVelocity, P.W(), UE_NETWORK_PHYSICS::V, TEXT("Angular Velocity")) ||
					CompareQuat(Obj.Rotation, P.R(), UE_NETWORK_PHYSICS::R, TEXT("Rotation")))
				{

					UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections, LogNetworkPhysics, Log, TEXT("Rewind Needed. Obj.Frame: %d. LastCompletedStep: %d."), Obj.Frame, LastCompletedStep);

					RewindToFrame = RewindToFrame == INDEX_NONE ? Obj.Frame : FMath::Min(RewindToFrame, Obj.Frame);
					ensure(RewindToFrame >= 0);

					PendingCorrections.Emplace(Obj);
					PendingCorrectionIdx = 0;
				}
			}
		}

		// Gameplay code extension
		for (Chaos::ISimCallbackObject* Callback : SimObjectCallbacks)
		{
			int32 CallbackFrame = Callback->TriggerRewindIfNeeded_Internal(LastCompletedStep);
			if (CallbackFrame != INDEX_NONE)
			{
				RewindToFrame = RewindToFrame == INDEX_NONE ? CallbackFrame : FMath::Min(RewindToFrame, CallbackFrame);
			}
		}

		// Calling code will ensure if we return a bad frame
		if (!(RewindToFrame > INDEX_NONE && RewindToFrame < LastCompletedStep-1))
		{
			UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections && RewindToFrame != INDEX_NONE, LogNetworkPhysics, Warning, TEXT("Trying to rewind to invalid frame %d (LastCompletedStep: %d)"), RewindToFrame, LastCompletedStep);
			return INDEX_NONE;
		}
		
		ResimEndFrame = RewindToFrame;
		return RewindToFrame;
	}
	
	void ProcessInputs_Internal(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) override
	{
		// Marhsall data back to GT based on what was requested for networking (this should be server only)
		FRequest Request;
		while (DataRequested.Dequeue(Request));

		FSnapshot Snapshot;
		for (FSingleParticlePhysicsProxy* Proxy : Request.Proxies)
		{
			if (auto* PT = Proxy->GetPhysicsThreadAPI())
			{
				FNetworkPhysicsState& Obj = Snapshot.Objects.AddDefaulted_GetRef();
				Obj.Proxy = Proxy;
				Obj.Frame = PhysicsStep;

				Obj.ObjectState = PT->ObjectState();
				Obj.Location = PT->X();
				Obj.LinearVelocity = PT->V();
				Obj.Rotation = PT->R();
				Obj.AngularVelocity = PT->W();
			}
		}

		if (Snapshot.Objects.Num() > 0)
		{
			DataFromPhysics.Enqueue(MoveTemp(Snapshot));
		}

		// ------------------------------------------------------------------------
		
		for (auto CallbacKInput : SimCallbackInputs)
		{
			for (Chaos::ISimCallbackObject* Callback : SimObjectCallbacks)
			{
				if (CallbacKInput.CallbackObject == Callback)
				{
					Callback->ApplyCorrections(PhysicsStep, CallbacKInput.Input);
					break;
				}
			}			
		}
	}

	void PreResimStep_Internal(int32 PhysicsStep, bool bFirst) override
	{
		// Apply Physics corrections if necessary
		if (PendingCorrectionIdx == INDEX_NONE)
		{
			return;
		}
		
		for (; PendingCorrectionIdx < PendingCorrections.Num(); ++PendingCorrectionIdx)
		{
			FNetworkPhysicsState& CorrectionState = PendingCorrections[PendingCorrectionIdx];
			if (CorrectionState.Frame > PhysicsStep)
			{
				break;
			}

			if (auto* PT = CorrectionState.Proxy->GetPhysicsThreadAPI())
			{
				UE_LOG(LogNetworkPhysics, Log, TEXT("Applying Correction from frame %d (actual step: %d)"), CorrectionState.Frame, PhysicsStep);

				PT->SetX(CorrectionState.Location);
				PT->SetV(CorrectionState.LinearVelocity);
				PT->SetR(CorrectionState.Rotation);
				PT->SetW(CorrectionState.AngularVelocity);

				if (PT->ObjectState() != CorrectionState.ObjectState)
				{
					ensure(CorrectionState.ObjectState != Chaos::EObjectStateType::Uninitialized);
					UE_LOG(LogNetworkPhysics, Log, TEXT("Applying Correction State %d"), CorrectionState.ObjectState);
					PT->SetObjectState(CorrectionState.ObjectState);
				}
			}
		}
	}

	void PostResimStep_Internal(int32 PhysicsStep) override
	{
		if (ResimEndFrame == PhysicsStep)
		{
			ResimEndFrame = INDEX_NONE;
		}
	}

	void ProcessInputs_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) override 
	{
		// Finalize our GT input for this PhysicsStep.
		if (World->GetNetMode() == NM_Client)
		{
			// Client:
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				// TODO: Broadcast delegate to generate new InputCmd from user code and call RPC to replicate it
				FNetBitWriter Writer(nullptr, 256 << 3);

				for (Chaos::FSimCallbackInputAndObject Pair :  SimCallbackInputs)
				{
					if (Pair.Input->NetSendInputCmd(Writer))
					{
						break;
					}
				}

				// Calling the RPC right here is bad. There probably is some intermediate buffer that these finalized commands should go to and the network
				// sending can happen elsewhere. That is also where move combining / delta compressiong type logic could happen.
				TArray<uint8> SendData;
				if (Writer.IsError() == false)
				{
					int32 NumBytes = (int32)Writer.GetNumBytes();
					SendData = MoveTemp(*const_cast<TArray<uint8>*>(Writer.GetBuffer()));
					SendData.SetNum(NumBytes);
				}
				
				PC->PushClientInput(PhysicsStep, SendData);
			}
		}
		else
		{
			// Server: look at bufferd received ClientInputFrames and consume (usually) one
			constexpr int32 MaxBufferedCmds = 64;

			for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
			{
				if (APlayerController* PC = Iterator->Get())
				{
					APlayerController::FServerFrameInfo& FrameInfo = PC->GetServerFrameInfo();
					APlayerController::FInputCmdBuffer& InputBuffer = PC->GetInputBuffer();

					auto UpdateLastProcessedInputFrame = [&]()
					{
						const int32 NumBufferedInputCmds = InputBuffer.HeadFrame() - FrameInfo.LastProcessedInputFrame;

						// Check Overflow
						if (NumBufferedInputCmds > MaxBufferedCmds)
						{
							UE_LOG(LogNetworkPhysics, Warning, TEXT("[Remote.Input] overflow %d %d -> %d"), InputBuffer.HeadFrame(), FrameInfo.LastProcessedInputFrame, NumBufferedInputCmds);
							FrameInfo.LastProcessedInputFrame = InputBuffer.HeadFrame() - MaxBufferedCmds + 1;
						}

						// Check fault - we are waiting for Cmds to reach FaultLimit before continuing
						if (FrameInfo.bFault)
						{
							if (NumBufferedInputCmds < FrameInfo.FaultLimit)
							{
								// Skip this because it is in fault. We will use the prev input for this frame.
								return;
							}
							FrameInfo.bFault = false;
						}
						else if (NumBufferedInputCmds == 0)
						{
							// No Cmds to process, enter fault state. Increment FaultLimit each time this happens.
							// TODO: We should have something to bring this back down (which means skipping frames) we don't want temporary poor conditions to cause permanent high input buffering
							FrameInfo.bFault = true;
							FrameInfo.FaultLimit = FMath::Min(FrameInfo.FaultLimit+1, MaxBufferedCmds-1);
							return;
						}

						FrameInfo.LastProcessedInputFrame++;
						FrameInfo.LastLocalFrame = PhysicsStep;
					};

					UpdateLastProcessedInputFrame();

					if (FrameInfo.LastProcessedInputFrame != INDEX_NONE)
					{
						const TArray<uint8>& Data = InputBuffer.Get(FrameInfo.LastProcessedInputFrame);
						if (Data.Num() > 0)
						{
							uint8* RawData = const_cast<uint8*>(Data.GetData());
							FNetBitReader Ar(nullptr, RawData, ((int64)Data.Num()) << 3);
						
							for (Chaos::FSimCallbackInputAndObject Pair :  SimCallbackInputs)
							{
								if (Pair.Input->NetRecvInputCmd(PC, Ar))
								{
									break;
								}
							}
						}
					}
				}
			}
		}
	}

	void RegisterRewindableSimCallback_Internal(Chaos::ISimCallbackObject* Callback) override
	{
		SimObjectCallbacks.Add(Callback);
	}

	struct FSnapshot
	{
		TArray<FNetworkPhysicsState> Objects;
	};

	TQueue<FSnapshot> DataFromNetwork;	// Data used to check for corrections
	TQueue<FSnapshot> DataFromPhysics;	// Data sent back to GT for networking

	// GT -> PT. "These are the proxies I want you to populate DataFromPhysics with". Is there a better way to do it?
	struct FRequest
	{
		TArray<FSingleParticlePhysicsProxy*> Proxies;
	};

	TQueue<FRequest> DataRequested;

	// Objects that we need to correct in PreResimStep_Internal
	TArray<FNetworkPhysicsState> PendingCorrections;
	int32 PendingCorrectionIdx = INDEX_NONE;
	int32 ResimEndFrame = INDEX_NONE;

	Chaos::FRewindData* RewindData=nullptr; // This be made to be accessed off of IRewindCallback
	Chaos::FPhysicsSolver* Solver = nullptr; //todo: shouldn't have to cache this, should be part of base class
	UWorld* World = nullptr;

	int32 LastResim = INDEX_NONE; // only used for force resim cvar

	// ----------------------

	TArray<Chaos::ISimCallbackObject*> SimObjectCallbacks;
};

// --------------------------------------------------------------------------------------------

UNetworkPhysicsManager::UNetworkPhysicsManager()
{
	
}

void UNetworkPhysicsManager::Initialize(FSubsystemCollectionBase& Collection)
{
	UWorld* World = GetWorld();
	check(World);

	if (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game)
	{
		if (UE_NETWORK_PHYSICS::bEnable)
		{
			PostTickDispatchHandle = World->OnPostTickDispatch().AddUObject(this, &UNetworkPhysicsManager::PostNetRecv); // Should be safe. We want to marshal GT->PT as soon as possible
			TickFlushHandle = World->OnTickFlush().AddUObject(this, &UNetworkPhysicsManager::PreNetSend); // Who wins, Subsystems or net drivers? We need to go first. Probably a dumb game to play (but we want to marshal PT->GT at last possible second)

			FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UNetworkPhysicsManager::OnWorldPostInit);
		}
	}
}

void UNetworkPhysicsManager::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (PostTickDispatchHandle.IsValid())
		{
			World->OnPostTickDispatch().Remove(PostTickDispatchHandle);
		}
		if (TickFlushHandle.IsValid())
		{
			World->OnTickFlush().Remove(TickFlushHandle);
		}
	}
}

void UNetworkPhysicsManager::OnWorldPostInit(UWorld* World, const UWorld::InitializationValues)
{
#if WITH_CHAOS
	if (World != GetWorld())
	{
		return;
	}

	FPhysScene* PhysScene = World->GetPhysicsScene();
	if (ensureAlways(PhysScene))
	{
		Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();

		if (ensure(Solver->GetRewindCallback()==nullptr))
		{
			Solver->EnableRewindCapture(64, true, MakeUnique<FNetworkPhysicsRewindCallback>());
			RewindCallback = static_cast<FNetworkPhysicsRewindCallback*>(Solver->GetRewindCallback());
			RewindCallback->RewindData = Solver->GetRewindData();
			RewindCallback->Solver = Solver;
			RewindCallback->World = World;
		}
	}
#endif
}

void UNetworkPhysicsManager::PostNetRecv()
{
#if WITH_CHAOS
	if (RewindCallback == nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	checkSlow(World);

	const bool bIsServer = World->GetNetMode() != NM_Client;

	// Iterate through all objects we are managing and coalease them into a snapshot that is marshalled to PT for rollback consideration
	if (bIsServer)
	{
		// Server has to tell PT what objects he wants to hear about.
		// This feels kind of not right but I'm not sure how else to do it from the PT alone? Can I get a list of active proxies on the PT?
		// I want a list of proxies on the PT that have rollback networking enable (currently defined by if they implement theINetworkPhysics interface)
		// (also maybe this should be in tick something? It doesn't need to be in post net recv)
		FNetworkPhysicsRewindCallback::FRequest Request;

		for (FNetworkPhysicsState* PhysicsState : ManagedPhysicsStates)
		{
			check(PhysicsState);
			check(PhysicsState->Proxy);

			Request.Proxies.Add(PhysicsState->Proxy);
		}

		if (Request.Proxies.Num() > 0)
		{
			RewindCallback->DataRequested.Enqueue(MoveTemp(Request));
		}
	}
	else
	{
		// Client: Marshal received data from Network to PT so it can be reconciled
		FNetworkPhysicsRewindCallback::FSnapshot Snapshot;
		int32 MaxFrameSeen = INDEX_NONE;
		
		// Calculate themost accurate LocalOffset so that we can coorelate ServerFrames with our local frames
		if (UE_NETWORK_PHYSICS::FixedLocalFrameOffset == INDEX_NONE)
		{
			// The server tells us what the latest InputCmd frame he processed from us when he did this tick (TODO)
			// There are N InputCmds in flight. This is precisely how many steps behind we should take this server frame number at.	
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				const APlayerController::FClientFrameInfo& ClientFrameInfo = PC->GetClientFrameInfo();
				const int32 LatestAckdClientFrame = ClientFrameInfo.LastRecvInputFrame;
				const int32 LatestAckdServerFrame = ClientFrameInfo.LastRecvServerFrame;
				if (LatestAckdClientFrame != INDEX_NONE)
				{
					const int32 FrameOffset = LatestAckdClientFrame - LatestAckdServerFrame;
					if (FrameOffset != LocalOffset)
					{
						UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections, LogNetworkPhysics, Log, TEXT("LocalOFfset Changed: %d -> %d"), LocalOffset, FrameOffset);
						LocalOffset = FrameOffset;
					}

					FPhysScene* PhysScene = World->GetPhysicsScene();
					checkSlow(PhysScene);			
					Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();
					checkSlow(Solver);

					//const int32 UnAckdFrames = Solver->GetCurrentFrame() - LatestAckdClientFrame;
					//UE_LOG(LogTemp, Warning, TEXT("%d UnAckdFrames"), UnAckdFrames);
				}
			}
		}
		else
		{
			// CVar that keeps us X +/- Y frames ahad of latest received server state
			int32 FrameOffset = 0;
			
			FPhysScene* PhysScene = World->GetPhysicsScene();
			checkSlow(PhysScene);			
			Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();
			checkSlow(Solver);

			int32 MaxFrameFromServer = INDEX_NONE;
			for (FNetworkPhysicsState* PhysicsState : ManagedPhysicsStates)
			{
				MaxFrameFromServer = FMath::Max(MaxFrameFromServer, PhysicsState->Frame);
			}

			// LocalFrame = ServerFrame + Offset
			FrameOffset = Solver->GetCurrentFrame() - UE_NETWORK_PHYSICS::FixedLocalFrameOffset - MaxFrameFromServer;

			if (FMath::Abs( LocalOffset - FrameOffset) > UE_NETWORK_PHYSICS::FixedLocalFrameOffsetTolerance)
			{
				UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections, LogNetworkPhysics, Warning, TEXT("LocalOFfset Changed: %d -> %d"), LocalOffset, FrameOffset);
				LocalOffset = FrameOffset;
			}
		}

		// Go through the physics states that we are managing and look for new network updates and marshal them to PT with correct local frame
		for (FNetworkPhysicsState* PhysicsState : ManagedPhysicsStates)
		{
			check(PhysicsState);
			MaxFrameSeen = FMath::Max(PhysicsState->Frame, MaxFrameSeen);
			if (PhysicsState->Frame > LastProcessedFrame)
			{
				const int32 LocalFrame = PhysicsState->Frame + LocalOffset;
				if (LocalFrame > 0 && PhysicsState->ObjectState != Chaos::EObjectStateType::Uninitialized)
				{
					FNetworkPhysicsState& MarshalledCopy = Snapshot.Objects.Emplace_GetRef(FNetworkPhysicsState(*PhysicsState));
					MarshalledCopy.Frame = LocalFrame;
				}
			}
		}

		if (Snapshot.Objects.Num() > 0)
		{
			RewindCallback->DataFromNetwork.Enqueue(MoveTemp(Snapshot));
		}

		for (auto& SubSys : SubSystems)
		{
			SubSys.Value->PostNetRecv(World, LocalOffset, LastProcessedFrame);
		}

		// The highest frame we encountered in the last network update is now sealed. We won't reconcile it again or any previous frames
		LastProcessedFrame = MaxFrameSeen;
	}
#endif
}

void UNetworkPhysicsManager::PreNetSend(float DeltaSeconds)
{
#if WITH_CHAOS
	if (RewindCallback == nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	checkSlow(World);

	for (auto& SubSys : SubSystems)
	{
		SubSys.Value->PreNetSend(World, DeltaSeconds);
	}


	const bool bIsServer = World->GetNetMode() != NM_Client;
	if (!bIsServer)
	{
		return;
	}

	FNetworkPhysicsRewindCallback::FSnapshot Snapshot;
	bool bFoundData = false;
	while (RewindCallback->DataFromPhysics.Dequeue(Snapshot))
	{			
		bFoundData = true;
	}

	if (bFoundData)
	{
		for (FNetworkPhysicsState& PhysicsState : Snapshot.Objects)
		{
			check(PhysicsState.Proxy);

			// Copy data that was marshalled from PT to the managed FNetworkPhysicsState* that will be replicated.
			// The tricky thing is that the objects we are hearing back from the PT about maybe deleted on the GT already.
			
			// If we could use the physics proxy to safely get to the FNetworkPhysicsState, that would be ideal.
			// The interface approach sort of allows this... if the interface is implemented on the actor. So not actually workable.
			// If we just merged FNetworkPhysicsState into FRepMovement, we could just cast to actor and be done...

			// INetworkPhysicsObject* NetworkObject = Cast<INetworkPhysicsObject>(PhysicsState.Proxy->GetOwner());
			// FNetworkPhysicsState* DestState = NetworkObject->GetNetworkPhysicsState();

			// So we have to do the map lookup for now...

			// Actually this wont work either because we aren't round tripping the LocalManagedHandle to the PT

			/*			
			if (int32* IdxPtr = ManagedHandleToIndexMap.Find(PhysicsState.LocalManagedHandle))			
			{
				const int32 Idx = *IdxPtr;
				if (ensure(ManagedPhysicsStates.IsValidIndex(Idx)))
				{
					*ManagedPhysicsStates[Idx] = PhysicsState;
				}
			}
			*/

			// This is how it has to work for now, linear search to match the proxy pointer.
			// We might be able to leverage consistent ordering (each linear search through both Snapshot.Objects and ManagedPhysicsStates incrementally in step)
			//	(but be careful in the case where GT is ahead of PT and later requests have newer objects in lower positions in the MangedPhysicsState list...)
			for (FNetworkPhysicsState* ManagedState : ManagedPhysicsStates)
			{
				if (ManagedState->Proxy == PhysicsState.Proxy)
				{
					ManagedState->Location = PhysicsState.Location;
					ManagedState->Rotation = PhysicsState.Rotation;
					ManagedState->LinearVelocity = PhysicsState.LinearVelocity;
					ManagedState->AngularVelocity = PhysicsState.AngularVelocity;
					ManagedState->Frame = PhysicsState.Frame;
					ManagedState->ObjectState = PhysicsState.ObjectState;
				}
			}
		}
	}
#endif
}

void UNetworkPhysicsManager::RegisterPhysicsProxy(FNetworkPhysicsState* State)
{
	checkSlow(State);
	State->LocalManagedHandle = ++UniqueHandleCounter;
	ManagedHandleToIndexMap.Add(UniqueHandleCounter) = ManagedPhysicsStates.EmplaceAtLowestFreeIndex(LastFreeIndex, State);
}

void UNetworkPhysicsManager::UnregisterPhysicsProxy(FNetworkPhysicsState* State)
{
	checkSlow(State);
	const int32 LocalHandle = State->LocalManagedHandle;

	if (LocalHandle != INDEX_NONE)
	{
		int32 LocalIndex;
		if (ensure(ManagedHandleToIndexMap.RemoveAndCopyValue(LocalHandle, LocalIndex)))
		{
			ManagedPhysicsStates.RemoveAt(LocalIndex);
			LastFreeIndex = FMath::Min(LastFreeIndex, LocalIndex);
		}
		
		State->LocalManagedHandle = INDEX_NONE;
	}
}

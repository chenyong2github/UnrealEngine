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

	int32 FixedLocalFrameOffset = 10;
	FAutoConsoleVariableRef CVarFixedLocalFrameOffset(TEXT("np2.FixedLocalFrameOffset"), FixedLocalFrameOffset, TEXT("When > 0, use hardcoded frame offset on client from head"));

	int32 FixedLocalFrameOffsetTolerance = 3;
	FAutoConsoleVariableRef CVarFixedLocalFrameOffsetTolerance(TEXT("np2.FixedLocalFrameOffsetTolerance"), FixedLocalFrameOffsetTolerance, TEXT("When > 0, use hardcoded frame offset on client from head"));
}

struct FNetworkPhysicsRewindCallback : public Chaos::IRewindCallback
{
	bool CompareVec(const FVector& A, const FVector& B, const float D, const TCHAR* Str)
	{
		const bool b = FVector::DistSquared(A, B) > D * D;
		UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections && b, LogNetworkPhysics, Log, TEXT("%s correction. Local: %s. Server: %s. Delta: %s (%d)"), Str, *A.ToString(), *B.ToString(), *(A-B).ToString(), (A-B).Size());
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

		// Fixme: we have to be careful not to return too early of a frame but not clear how to get it from here
		const int32 MinFrame = LastCompletedStep - 64;
		
		PendingCorrectionIdx = INDEX_NONE;
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
					UE_LOG(LogNetworkPhysics, Warning, TEXT("Obj too stale to reconcile. Frmae: %d. LastCompletedStep: %d"), Obj.Frame, LastCompletedStep);
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

		if (!(RewindToFrame > INDEX_NONE && RewindToFrame < LastCompletedStep-1))
		{
			return INDEX_NONE;
		}
		
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

		
	}

	void PreResimStep_Internal(int32 PhysicsStep, bool bFirst) override
	{
		// ===============================================
		// Apply Physics corrections if necessary
		// ===============================================
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
		

	}

	void ProcessInputs_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) override 
	{

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

	Chaos::FRewindData* RewindData=nullptr; // This be made to be accessed off of IRewindCallback
	Chaos::FPhysicsSolver* Solver = nullptr; //todo: shouldn't have to cache this, should be part of base class

	int32 LastResim = INDEX_NONE; // only used for force resim cvar
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

	const bool bIsServer = GetWorld()->GetNetMode() != NM_Client;

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
		
		// This is where we remap server frame to our local frame.
		// The server tells us what the latest InputCmd frame he processed from us when he did this tick (TODO)
		// There are N InputCmds in flight. This is precisely how many steps behind we should take this server frame number at.
		//
		// This really should be a global thing for the client. It doesn't need to be tied to a specific object.
		
		{
			int32 FrameOffset = 0;

			// Hack: client tries to be 10 frames ahead of latest server data
			FPhysScene* PhysScene = GetWorld()->GetPhysicsScene();
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
				UE_LOG(LogNetworkPhysics, Warning, TEXT("LocalOFfset Changed: %d -> %d"), LocalOffset, FrameOffset);
				LocalOffset = FrameOffset;
			}
		}

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

		// The highest frame we encountered in the last network update is now sealed. We won't reconcile it again or any previous frames
		LastProcessedFrame = MaxFrameSeen;
	}
#endif
}

void UNetworkPhysicsManager::PreNetSend(float DeltaSeconds)
{
	const bool bIsServer = GetWorld()->GetNetMode() != NM_Client;
	if (RewindCallback == nullptr || !bIsServer)
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
			// We miight be able to leverage consistent ordering (each linear search through both Snapshot.Objects and ManagedPhysicsStates incrementally in step)
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

// ========================================================================================

UNetworkPhysicsComponent::UNetworkPhysicsComponent()
{
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);
}

void UNetworkPhysicsComponent::InitializeComponent()
{
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

#if WITH_CHAOS
	if (ensureMsgf(PrimitiveComponent, TEXT("No PrimitiveComponent found on %s"), *GetPathName()))
	{
		NetworkPhysicsState.Proxy = PrimitiveComponent->BodyInstance.ActorHandle;
		Manager->RegisterPhysicsProxy(&NetworkPhysicsState);
	}
#endif
}

void UNetworkPhysicsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	if (UWorld* World = GetWorld())
	{
		World->GetSubsystem<UNetworkPhysicsManager>()->UnregisterPhysicsProxy(&NetworkPhysicsState);
	}
}

void UNetworkPhysicsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME( UNetworkPhysicsComponent, NetworkPhysicsState);
}
// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionAsync.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

// ---------------------------------------------------------------------

namespace UE_NETWORK_PHYSICS
{
	bool TempMockDebug=true;
	FAutoConsoleVariableRef CVarTempMockDebug(TEXT("np2.Mock.Debug"), TempMockDebug, TEXT("Enabled spammy log debugging of mock physics object state"));

	bool bEnableAsync=false;
	FAutoConsoleVariableRef CVarbEnableAsync(TEXT("np2.Async.Enable"), bEnableAsync, TEXT("Enable Async implementation"));
}

struct FTempMockObjModlDef : public UE_NP::TNetworkPredictionModelDefAsync<FTempMockObjModlDef>
{
	using InputCmdType = FTempMockInputCmd;
	using ObjStateType = FTempMockObject;
	
	using ObjKeyType = UTempMockComponent*;
	static const TCHAR* GetName() { return TEXT("TempMockObj"); }
};

bool FNetworkPredictionProxyAsync::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	if (!UE_NETWORK_PHYSICS::bEnableAsync)
	{
		bOutSuccess = true;
		return true;
	}

	if (ensure(NetSerializeFunc))
	{
		NetSerializeFunc(Ar);
		bOutSuccess = true;
		return true;
	}

	bOutSuccess = false;
	return false;
}

void FTempMockObject::SimulationTick(const FTempMockInputCmd* InputCmd, FTempMockObject* SimObject, const float DeltaSeconds, const int32 Frame, const int32 LocalStorageFrame)
{
	// UE_LOG(LogTemp, Warning, TEXT("SimulationTick. %d. Proxy: 0x%X"), Frame, (int64)SimObject->Proxy);
	
	if (ensure(SimObject->Proxy))
	{
		if (auto* PT = SimObject->Proxy->GetPhysicsThreadAPI())
		{
			if (InputCmd->bBrakesPressed)
			{
				PT->SetV( Chaos::FVec3(0.f));				
				PT->SetW( Chaos::FVec3(0.f));

				UE_CLOG(UE_NETWORK_PHYSICS::TempMockDebug, LogNetworkPhysics, Log, TEXT("[%d/%d] Applied Break. Rot was: %s"), Frame, LocalStorageFrame, *PT->R().Rotator().ToString());
				Chaos::FRotation3 NewR = PT->R();
				FRotator Rot = NewR.Rotator();
				Rot.Pitch = 0.f;
				Rot.Roll = 0.f;
				PT->SetR( FQuat(Rot));
			}
			else
			{
				if (InputCmd->Force.SizeSquared() > 0.001f)
				{
					//UE_LOG(LogTemp, Warning, TEXT("Applied Force. Frame: %d"), LocalFrame);
					//UE_LOG(LogTemp, Warning, TEXT("0x%X ForceMultiplier: %f (Rand: %d)"), (int64)Proxy, GT_State.ForceMultiplier, GT_State.RandValue);
					//UE_CLOG(PC == nullptr && InputCmd.Force.SizeSquared() > 0.f, LogTemp, Warning, TEXT("0x%X ForceMultiplier: %f (Rand: %d)"), (int64)Proxy, GT_State.ForceMultiplier, GT_State.RandValue);
					PT->AddForce(InputCmd->Force * SimObject->ForceMultiplier);
				}
				
				if (FMath::Abs<float>(InputCmd->Turn) > 0.001f)
				{
					PT->AddTorque(Chaos::FVec3(0.0f, 0.0f, InputCmd->Turn * SimObject->ForceMultiplier * 10.f));
				}				
			}

			if (InputCmd->bJumpedPressed)
			{
				SimObject->JumpCount++;
				UE_CLOG(UE_NETWORK_PHYSICS::TempMockDebug, LogNetworkPhysics, Log, TEXT("[%d/%d] 0x%X. bJumpedPressed: %d. Count: %d"), Frame, LocalStorageFrame, (int64)SimObject, InputCmd->bJumpedPressed, SimObject->JumpCount);
			}

			//UE_CLOG(PC == nullptr, LogTemp, Warning, TEXT("NP [%d] bJumpedPressed: %d. Count: %d"), LocalFrame, InputCmd.bJumpedPressed, PT_State.JumpCount);
			if (InputCmd->bJumpedPressed && SimObject->JumpCooldownMS == 0)
			{
				//UE_LOG(LogTemp, Warning, TEXT("0x%X Jump! Total: %d"), (int64)Proxy, (int32)(TotalSeconds*1000.f));
				
				PT->SetLinearImpulse( Chaos::FVec3(0.f, 0.f, 115000.f) );

				SimObject->JumpCooldownMS = 1000;
				UE_CLOG(UE_NETWORK_PHYSICS::TempMockDebug, LogNetworkPhysics, Log, TEXT("[%d/%d] Applied Jump and reset cooldown"), Frame, LocalStorageFrame);
			}
			else
			{
				SimObject->JumpCooldownMS = FMath::Max( SimObject->JumpCooldownMS - (int32)(DeltaSeconds* 1000.f), 0);
				if (SimObject->JumpCooldownMS != 0)
				{
					UE_CLOG(UE_NETWORK_PHYSICS::TempMockDebug, LogNetworkPhysics, Log, TEXT("[%d/%d] JumpCount: %d. JumpCooldown: %d"), Frame, LocalStorageFrame, SimObject->JumpCount, SimObject->JumpCooldownMS);
				}
			}
		}
	}
}


UTempMockComponent::UTempMockComponent()
{
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UTempMockComponent::InitializeComponent()
{
	Super::InitializeComponent();

#if WITH_CHAOS
	if (!UE_NETWORK_PHYSICS::bEnableAsync)
	{
		return;
	}

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
		NetworkPhysicsState.OwningActor = GetOwner();
		ensure(NetworkPhysicsState.OwningActor);

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

		
		UE_NP::TNetworkPredictionAsyncSystemManager<FTempMockObjModlDef>* MockManager = UE_NP::TNetworkPredictionAsyncSystemManager<FTempMockObjModlDef>::Get(World);
		checkSlow(MockManager);

		FTempMockObject InitialState;
		InitialState.Proxy = PrimitiveComponent->BodyInstance.ActorHandle;
		MockManager->RegisterNewInstance(this, InitialState);

		ReplicatedObject.InitProxy(MockManager, this);
	}

#endif
}

void UTempMockComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

#if WITH_CHAOS
	if (!UE_NETWORK_PHYSICS::bEnableAsync)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UNetworkPhysicsManager* Manager = World->GetSubsystem<UNetworkPhysicsManager>())
		{
			Manager->UnregisterPhysicsProxy(&NetworkPhysicsState);

			if (auto* MockManager = UE_NP::TNetworkPredictionAsyncSystemManager<FTempMockObjModlDef>::Get(World))
			{
				MockManager->UnregisterInstance(this);
			}
		}
	}
#endif
}

void UTempMockComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME( UTempMockComponent, NetworkPhysicsState);
	DOREPLIFETIME( UTempMockComponent, ReplicatedObject);
}

void UTempMockComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if WITH_CHAOS
	if (!UE_NETWORK_PHYSICS::bEnableAsync)
	{
		return;
	}

	APlayerController* PC = GetOwnerPC();
	
	// Detect changes in possession (this isn't ideal: would rather register a delegate to tell us when this happens on the pawn but it doesn't exist)
	if (CachedPC != PC)
	{
		if (UE_NP::TNetworkPredictionAsyncSystemManager<FTempMockObjModlDef>* MockManager = UE_NP::TNetworkPredictionAsyncSystemManager<FTempMockObjModlDef>::Get(GetWorld()))
		{
			MockManager->RegisterController(this, PC, (PC && PC->IsLocalController()) ? &PendingInputCmd : nullptr);
		}
		CachedPC = PC;
	}

	if (PC && PC->IsLocalController())
	{
		// Broadcast out a delegate. The user will write to PendingInputCmd
		OnGeneratedLocalInputCmd.Broadcast();
	}
#endif
}

APlayerController* UTempMockComponent::GetOwnerPC() const
{
	if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(PawnOwner->GetController());
	}

	return nullptr;
}
// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionAsync.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "NetworkPredictionCVars.h"

// ---------------------------------------------------------------------

namespace UE_NETWORK_PHYSICS
{
	NETSIM_DEVCVAR_SHIPCONST_INT(bEnableAsync, 0, "np2.Async.Enable", "Enable Async implementation");
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
	if (!UE_NETWORK_PHYSICS::bEnableAsync())
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

void FTempMockObject::SimulationTick(UWorld* World, const FTempMockInputCmd* InputCmd, FTempMockObject* SimObject, const float DeltaSeconds, const int32 SimulationFrame, const int32 LocalStorageFrame)
{
	if (!ensure(SimObject->Proxy))
	{
		return;
	}

	if (auto* PT = SimObject->Proxy->GetPhysicsThreadAPI())
	{
		UE_NETWORK_PHYSICS::ConditionalFrameEnsure();
		if (UE_NETWORK_PHYSICS::ConditionalFrameBreakpoint())
		{
			UE_LOG(LogTemp, Warning, TEXT("[%d][%d] Location: %s"), UE_NETWORK_PHYSICS::DebugSimulationFrame(), UE_NETWORK_PHYSICS::DebugServer(), *PT->X().ToString());
		}

		FVector TracePosition = PT->X();
		FVector EndPosition = TracePosition + FVector(0.f, 0.f, -100.f);
		FCollisionShape Shape = FCollisionShape::MakeSphere(250.f);
		ECollisionChannel CollisionChannel = ECollisionChannel::ECC_WorldStatic; 
		FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;

		FCollisionResponseParams ResponseParams = FCollisionResponseParams::DefaultResponseParam;
		FCollisionObjectQueryParams ObjectParams(ECollisionChannel::ECC_PhysicsBody);

		FHitResult OutHit;
		const bool bInAir = !UE_NETWORK_PHYSICS::JumpHack() && !World->LineTraceSingleByChannel(OutHit, TracePosition, EndPosition, ECollisionChannel::ECC_WorldStatic, QueryParams, ResponseParams);
		const float UpDot = FVector::DotProduct(PT->R().GetUpVector(), FVector::UpVector);

		if (UE_NETWORK_PHYSICS::JumpMisPredict())
		{
			if (InputCmd->bJumpedPressed && SimObject->JumpStartFrame + 10 < SimulationFrame)
			{
				SimObject->JumpStartFrame = SimulationFrame;
				PT->SetLinearImpulse(FVector(0.f, 0.f, 60000.f + (FMath::FRand() * 50000.f)));
			}
			return;
		}

		if (UE_NETWORK_PHYSICS::MockImpulse() && SimObject->KickFrame + 10 < SimulationFrame)
		{
			/*
			for (FSingleParticlePhysicsProxy* BallProxy : BallProxies)
			{					
				if (auto* BallPT = BallProxy->GetPhysicsThreadAPI())
				{
					const FVector BallLocation = BallPT->X();
					const float BallRadius = BallPT->Geometry()->BoundingBox().OriginRadius(); //GetRadius();
						
					if (BallRadius > 0.f && (FVector::DistSquared(PT->X(), BallLocation) < BallRadius * BallRadius))
					{
						FVector Impulse = BallPT->X() - PT->X();
						Impulse.Z =0.f;
						Impulse.Normalize();
						Impulse *= UE_NETWORK_PHYSICS::MockImpulseX;
						Impulse.Z = UE_NETWORK_PHYSICS::MockImpulseZ();

						//UE_LOG(LogTemp, Warning, TEXT("Applied Force. %s"), *GetNameSafe(HitPrimitive->GetOwner()));
						BallPT->SetLinearImpulse( Impulse, false );
						SimObject->KickFrame = SimulationFrame;
					}
				}
			}
			*/
		}

		// ---------------------------------------------------------------------------------------------
						
		if (!bInAir)
		{
			if (SimObject->InAirFrame != 0)
			{
				SimObject->InAirFrame = 0;
				//SimObject->JumpStartFrame = 0;
			}

			// Check for recovery start
			if (SimObject->RecoveryFrame == 0)
			{
				if (UpDot < 0.2f)
				{
					SimObject->RecoveryFrame = SimulationFrame;
				}
			}
		}
		else
		{
			if (SimObject->InAirFrame == 0)
			{
				SimObject->InAirFrame = SimulationFrame;
			}
		}
			
		//UE_LOG(LogNetworkPhysics, Log, TEXT("[%d] AirFrame: %d. JumpFrame: %d"), SimulationFrame, SimObject->InAirFrame, SimObject->JumpStartFrame);
		if (InputCmd->bJumpedPressed)
		{
			if (SimObject->InAirFrame == 0 || (SimObject->InAirFrame + UE_NETWORK_PHYSICS::JumpFudgeFrames() > SimulationFrame))
			{
				if (SimObject->JumpStartFrame == 0)
				{
					SimObject->JumpStartFrame = SimulationFrame;
				}

				if (SimObject->JumpStartFrame + UE_NETWORK_PHYSICS::JumpFrameDuration() > SimulationFrame)
				{
					PT->AddForce( Chaos::FVec3(0.f, 0.f, UE_NETWORK_PHYSICS::JumpForce()) );
					
					//UE_LOG(LogTemp, Warning, TEXT("[%d] Jumped [JumpStart: %d. InAir: %d]"), SimulationFrame, SimObject->JumpStartFrame, SimObject->InAirFrame);
					SimObject->JumpCooldownMS = 1000;
				}
			}
		}
		else
		{
			if (SimObject->InAirFrame == 0 && (SimObject->JumpStartFrame + UE_NETWORK_PHYSICS::JumpFrameDuration() < SimulationFrame))
			{
				SimObject->JumpStartFrame = 0;
			}
		}

		if (SimObject->RecoveryFrame != 0)
		{
			if (UpDot > 0.7f)
			{
				// Recovered
				SimObject->RecoveryFrame = 0;
			}
			else
			{
				// Doing it per-axis like this is probably wrong
				FRotator Rot = PT->R().Rotator();
				const float DeltaRoll = FRotator::NormalizeAxis( -1.f * (Rot.Roll + (PT->W().X * UE_NETWORK_PHYSICS::TurnDampK())));
				const float DeltaPitch = FRotator::NormalizeAxis( -1.f * (Rot.Pitch + (PT->W().Y * UE_NETWORK_PHYSICS::TurnDampK())));

				PT->AddTorque(FVector(DeltaRoll, DeltaPitch, 0.f) * UE_NETWORK_PHYSICS::TurnK() * 1.5f);
				PT->AddForce(FVector(0.f, 0.f, 600.f));
			}
		}
		else if (InputCmd->bBrakesPressed)
		{
			FVector NewV = PT->V();
			if (NewV.SizeSquared2D() < 1.f)
			{
				PT->SetV( Chaos::FVec3(0.f, 0.f, NewV.Z));
			}
			else
			{
				PT->SetV( Chaos::FVec3(NewV.X * 0.8f, NewV.Y * 0.8f, NewV.Z));
			}
		}
		else
		{
			// Movement
			if (InputCmd->Force.SizeSquared() > 0.001f)
			{
				PT->AddForce(InputCmd->Force * SimObject->ForceMultiplier * UE_NETWORK_PHYSICS::MovementK());

				// Auto Turn
				const float CurrentYaw = PT->R().Rotator().Yaw + (PT->W().Z * UE_NETWORK_PHYSICS::TurnDampK());
				const float DesiredYaw = InputCmd->Force.Rotation().Yaw;
				const float DeltaYaw = FRotator::NormalizeAxis( DesiredYaw - CurrentYaw );
					
				PT->AddTorque(FVector(0.f, 0.f, DeltaYaw * UE_NETWORK_PHYSICS::TurnK()));
			}
		}

		// Drag force
		FVector V = PT->V();
		V.Z = 0.f;
		if (V.SizeSquared() > 0.1f)
		{
			FVector Drag = -1.f * V * UE_NETWORK_PHYSICS::DragK();
			PT->AddForce(Drag);
		}
			
		SimObject->JumpCooldownMS = FMath::Max( SimObject->JumpCooldownMS - (int32)(DeltaSeconds* 1000.f), 0);
		if (SimObject->JumpCooldownMS != 0)
		{
			UE_CLOG(UE_NETWORK_PHYSICS::MockDebug(), LogNetworkPhysics, Log, TEXT("[%d/%d] JumpCount: %d. JumpCooldown: %d"), SimulationFrame, LocalStorageFrame, SimObject->JumpCount, SimObject->JumpCooldownMS);
		}

		if (InputCmd->bJumpedPressed)
		{
			// Note this is really just for debugging. "How many times was the button pressed"
			SimObject->JumpCount++;
			UE_CLOG(UE_NETWORK_PHYSICS::MockDebug(), LogNetworkPhysics, Log, TEXT("[%d/%d] bJumpedPressed: %d. Count: %d"), SimulationFrame, LocalStorageFrame, InputCmd->bJumpedPressed, SimObject->JumpCount);
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
	if (!UE_NETWORK_PHYSICS::bEnableAsync())
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
	if (!UE_NETWORK_PHYSICS::bEnableAsync())
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
	if (!UE_NETWORK_PHYSICS::bEnableAsync())
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
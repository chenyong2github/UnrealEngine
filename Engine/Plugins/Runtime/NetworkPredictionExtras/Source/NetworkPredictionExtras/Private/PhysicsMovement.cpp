// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsMovement.h"
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
#include "TimerManager.h"
#include "NetworkPredictionDebug.h"
#include "NetworkPredictionCVars.h"
#include "Async/NetworkPredictionAsyncModelDef.h"
#include "Async/NetworkPredictionAsyncWorldManager.h"
#include "Async/NetworkPredictionAsyncID.h"
#include "Async/NetworkPredictionAsyncModelDefRegistry.h"
#include "Async/NetworkPredictionAsyncProxyImpl.h"
#include "Components/PrimitiveComponent.h"


struct FPhysicsMovementSimulation
{
	static void Tick_Internal(FNetworkPredictionAsyncTickContext& Context, UE_NP::FNetworkPredictionAsyncID ID, FPhysicsInputCmd& InputCmd, FPhysicsMovementLocalState& LocalState, FPhysicsMovementNetState& NetState)
	{
		UWorld* World = Context.World;
		const float DeltaSeconds = Context.DeltaTimeSeconds;
		const int32 SimulationFrame = Context.SimulationFrame;

		if (!ensureAlways(LocalState.Proxy))
		{
			return;
		}

		auto* PT = LocalState.Proxy->GetPhysicsThreadAPI();
		if (!PT)
		{
			return;
		}

		if (!InputCmd.bLegit)
		{
			// THis is just debug warning trying to make sure we never submit uninitialized input cmds into the system.
			UE_LOG(LogNetworkPrediction, Warning, TEXT("Illegitimate InputCmd has seeped in!!! SimFrame: %d. LocalFrame: %d"), SimulationFrame, Context.LocalStorageFrame);
		}

		//UE_LOG(LogTemp, Warning, TEXT("[%s][%d][%d] Yaw: %f %s"), World->GetNetMode() == NM_Client ? TEXT("C") : TEXT("S"), SimulationFrame, SimulationFrame - Context.LocalStorageFrame, InputCmd.TargetYaw, Context.bIsResim ? TEXT("RESIM") : TEXT(" "));
		//UE_LOG(LogTemp, Warning, TEXT("[%s][%d][%d] 0x%X Count: %d %s"), World->GetNetMode() == NM_Client ? TEXT("C") : TEXT("S"), SimulationFrame, SimulationFrame - Context.LocalStorageFrame, (int64)&InputCmd, InputCmd.Counter, Context.bIsResim ? TEXT("RESIM") : TEXT(" "));

		//UE_LOG(LogTemp, Warning, TEXT("0x%X [%s][%d][%d][%d] ID: %d. CheckSum: %d %s"), (int64)World, World->GetNetMode() == NM_Client ? TEXT("C") : TEXT("S"), SimulationFrame, Context.LocalStorageFrame, SimulationFrame - Context.LocalStorageFrame, (int32)ID, NetState.CheckSum, Context.bIsResim ? TEXT("RESIM") : TEXT(" "));
		NetState.CheckSum++;

		//UE_LOG(LogTemp, Warning, TEXT("[%d] R: %s W: %s"), SimulationFrame, *PT->R().ToString(), *PT->W().ToString());

		UE_NETWORK_PHYSICS::ConditionalFrameEnsure();
		if (UE_NETWORK_PHYSICS::ConditionalFrameBreakpoint())
		{
			UE_LOG(LogTemp, Warning, TEXT("[%d][%d] Location: %s"), UE_NETWORK_PHYSICS::DebugSimulationFrame(), UE_NETWORK_PHYSICS::DebugServer(), *PT->X().ToString());
		}

		FVector TracePosition = PT->X();
		FVector EndPosition = TracePosition + FVector(0.f, 0.f, -100.f);
		FCollisionShape Shape = FCollisionShape::MakeSphere(250.f);
		ECollisionChannel CollisionChannel = ECollisionChannel::ECC_WorldStatic;
		FCollisionResponseParams ResponseParams = FCollisionResponseParams::DefaultResponseParam;
		FCollisionObjectQueryParams ObjectParams(ECollisionChannel::ECC_PhysicsBody);

		FHitResult OutHit;
		bool bInAir = !UE_NETWORK_PHYSICS::JumpHack() && !World->LineTraceSingleByChannel(OutHit, TracePosition, EndPosition, ECollisionChannel::ECC_WorldStatic, LocalState.QueryParams, ResponseParams);
		const float UpDot = FVector::DotProduct(PT->R().GetUpVector(), FVector::UpVector);

		if (UPrimitiveComponent* HitPrim = OutHit.Component.Get())
		{
			if (FPhysicsInterface::IsValid(HitPrim->BodyInstance.ActorHandle) && (LocalState.Proxy == HitPrim->BodyInstance.ActorHandle))
			{
				ensure(false); // hit self somehow?
			}
		}

		// Debug CVar to make jump cause mispredictions
		if (UE_NETWORK_PHYSICS::JumpMisPredict())
		{
			if (InputCmd.bJumpedPressed && NetState.JumpStartFrame + 10 < SimulationFrame)
			{
				NetState.JumpStartFrame = SimulationFrame;
				PT->SetLinearImpulse(FVector(0.f, 0.f, 60000.f + (FMath::FRand() * 50000.f)));
			}
			return;
		}

		// Impulse on physics ball (not reimplemented yet)
		if (UE_NETWORK_PHYSICS::MockImpulse() && NetState.KickFrame + 10 < SimulationFrame)
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
						Impulse *= UE_NETWORK_PHYSICS::MockImpulseX();
						Impulse.Z = UE_NETWORK_PHYSICS::MockImpulseZ();

						//UE_LOG(LogTemp, Warning, TEXT("Applied Force. %s"), *GetNameSafe(HitPrimitive->GetOwner()));
						BallPT->SetLinearImpulse( Impulse, false );
						NetState.KickFrame = SimulationFrame;
					}
				}
			}
			*/
		}

		if (!bInAir)
		{
			if (NetState.InAirFrame != 0)
			{
				NetState.InAirFrame = 0;
			}
		}
		else
		{
			if (NetState.InAirFrame == 0)
			{
				NetState.InAirFrame = SimulationFrame;
			}
		}

		if (InputCmd.bJumpedPressed)
		{
			if (NetState.InAirFrame == 0 || (NetState.InAirFrame + UE_NETWORK_PHYSICS::JumpFudgeFrames() > SimulationFrame))
			{
				if (NetState.JumpStartFrame == 0)
				{
					NetState.JumpStartFrame = SimulationFrame;
				}

				if (NetState.JumpStartFrame + UE_NETWORK_PHYSICS::JumpFrameDuration() > SimulationFrame)
				{
					PT->AddForce( Chaos::FVec3(0.f, 0.f, UE_NETWORK_PHYSICS::JumpForce()) );

					//UE_LOG(LogTemp, Warning, TEXT("[%d] Jumped [JumpStart: %d. InAir: %d]"), SimulationFrame, NetState.JumpStartFrame, NetState.InAirFrame);
					NetState.JumpCooldownMS = 1000;
				}
			}
		}
		else
		{
			if (NetState.InAirFrame == 0 && (NetState.JumpStartFrame + UE_NETWORK_PHYSICS::JumpFrameDuration() < SimulationFrame))
			{
				NetState.JumpStartFrame = 0;
			}
		}
		
		// Keep pawn upright
		if (NetState.bEnableKeepUpright && UpDot < 0.95f)
		{
			FRotator Rot = PT->R().Rotator();
			FVector Target(0, 0, 1); // TODO choose up based on floor normal
			FVector CurrentUp = Rot.RotateVector(Target);
			FVector Torque = -FVector::CrossProduct(Target, CurrentUp);
			FVector Damp(PT->W().X* UE_NETWORK_PHYSICS::TurnDampK(), PT->W().Y* UE_NETWORK_PHYSICS::TurnDampK(), 0);

			PT->AddTorque(Torque* UE_NETWORK_PHYSICS::TurnK() + Damp);
		}

		if (InputCmd.bBrakesPressed)
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
			if (InputCmd.Force.SizeSquared() > 0.001f)
			{
				PT->AddForce(InputCmd.Force * NetState.ForceMultiplier * UE_NETWORK_PHYSICS::MovementK());

			}
			else if(!bInAir)
			{
				// Auto brake: Applied when no input and grounded.
				FVector NewV = PT->V();
				if (NewV.SizeSquared2D() < 1.f)
				{
					PT->SetV(Chaos::FVec3(0.f, 0.f, NewV.Z));
				}
				else
				{
					const float DragFactor = FMath::Max(0.f, FMath::Min(1.f - (NetState.AutoBrakeStrength * DeltaSeconds), 1.f));
					PT->SetV(Chaos::FVec3(NewV.X * DragFactor, NewV.Y * DragFactor, NewV.Z));
				}
			}

			// Rotation
			if (InputCmd.Torque.SizeSquared() > 0.001f)
			{
				PT->AddTorque(InputCmd.Torque* NetState.ForceMultiplier* UE_NETWORK_PHYSICS::RotationK());
			}

			if (NetState.bEnableAutoFaceTargetYaw)
			{
				// Auto Turn to target yaw
				const float CurrentYaw = PT->R().Rotator().Yaw + (PT->W().Z * NetState.AutoFaceTargetYawDamp);
				const float DesiredYaw = FMath::DegreesToRadians(InputCmd.TargetYaw);
				const float DeltaYaw = FRotator::NormalizeAxis(InputCmd.TargetYaw - CurrentYaw );
				PT->AddTorque(FVector(0.f, 0.f, DeltaYaw * NetState.AutoFaceTargetYawStrength));
			}
			else
			{
				// Prevent spheres from spinning in place.
				FVector AngularVelocity = PT->W();
				const float DragFactor = FMath::Max(0.f, FMath::Min(1.f - (UE_NETWORK_PHYSICS::DampYawVelocityK() * DeltaSeconds), 1.f));
				PT->SetW(Chaos::FVec3(AngularVelocity.X, AngularVelocity.Y, AngularVelocity.Z * DragFactor));
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
		
		// angular velocity limit
		FVector W = PT->W();
		{
			const float MaxAngularVelocitySq = UE_NETWORK_PHYSICS::MaxAngularVelocity() * UE_NETWORK_PHYSICS::MaxAngularVelocity();
			if (W.SizeSquared() > MaxAngularVelocitySq)
			{
				W = W.GetUnsafeNormal() * UE_NETWORK_PHYSICS::MaxAngularVelocity();
				PT->SetW(W);
			}
		}

		NetState.JumpCooldownMS = FMath::Max( NetState.JumpCooldownMS - (int32)(DeltaSeconds* 1000.f), 0);
		if (NetState.JumpCooldownMS != 0)
		{
			UE_CLOG(UE_NETWORK_PHYSICS::MockDebug(), LogNetworkPhysics, Log, TEXT("[%d/%d] JumpCount: %d. JumpCooldown: %d"), SimulationFrame, Context.LocalStorageFrame, NetState.JumpCount, NetState.JumpCooldownMS);
		}

		if (InputCmd.bJumpedPressed)
		{
			// Note this is really just for debugging. "How many times was the button pressed"
			NetState.JumpCount++;
			UE_CLOG(UE_NETWORK_PHYSICS::MockDebug(), LogNetworkPhysics, Log, TEXT("[%d/%d] bJumpedPressed: %d. Count: %d"), SimulationFrame, Context.LocalStorageFrame, InputCmd.bJumpedPressed, NetState.JumpCount);
		}
	}
};


struct FPhysicsMovementAsyncModelDef : public UE_NP::FNetworkPredictionAsyncModelDef
{
	NP_ASYNC_MODEL_BODY()

	using InputCmdType = FPhysicsInputCmd;
	using NetStateType = FPhysicsMovementNetState;
	using LocalStateType = FPhysicsMovementLocalState;
	using SimulationTickType = FPhysicsMovementSimulation;

	static const TCHAR* GetName() { return TEXT("PhysicsMovement"); }
	static constexpr int32 GetSortPriority() { return 0; }
};

NP_ASYNC_MODEL_REGISTER(FPhysicsMovementAsyncModelDef);


UPhysicsMovementComponent::UPhysicsMovementComponent()
{
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UPhysicsMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
#if WITH_CHAOS
	UWorld* World = GetWorld();	
	checkSlow(World);
	UNetworkPhysicsManager* Manager = World->GetSubsystem<UNetworkPhysicsManager>();
	if (!Manager)
	{
		return;
	}

	
	FPhysicsMovementLocalState LocalState;
	LocalState.Proxy = this->GetManagedProxy();
	LocalState.QueryParams.AddIgnoredActor(GetOwner());
	if (LocalState.Proxy)
	{
		if (ensure(NetworkPredictionProxy.RegisterProxy(GetWorld())))
		{
			NetworkPredictionProxy.RegisterSim<FPhysicsMovementAsyncModelDef>(MoveTemp(LocalState), FPhysicsMovementNetState(), &PendingInputCmd, &MovementState);
		}
	}
	else
	{
		UE_LOG(LogNetworkPhysics, Warning, TEXT("No valid physics body found on %s"), *GetName());
	}
#endif
}

void UPhysicsMovementComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);
	NetworkPredictionProxy.OnPreReplication();
}

void UPhysicsMovementComponent::UninitializeComponent()
{
	Super::UninitializeComponent();
	NetworkPredictionProxy.UnregisterProxy();
}

void UPhysicsMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	APlayerController* PC = GetOwnerPC();

	// RegisterController when owning PC changes, or at least once (With null controller) on server
	if (CachedPC != PC || (!bHasRegisteredController && GetOwnerRole() == ROLE_Authority))
	{
		NetworkPredictionProxy.RegisterController(PC);
		bHasRegisteredController = true;
		CachedPC = PC;
	}

	if ((PC && PC->IsLocalController()) || (GetOwnerRole() == ROLE_Authority))
	{
		// Broadcast out a delegate. The user will write to PendingInputCmd
		OnGenerateLocalInputCmd.Broadcast();
		PendingInputCmd.bLegit = true;
		//PendingInputCmd.Counter++;
	}
}

void UPhysicsMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME( UPhysicsMovementComponent, NetworkPredictionProxy);
}

APlayerController* UPhysicsMovementComponent::GetOwnerPC() const
{
	if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(PawnOwner->GetController());
	}

	return nullptr;
}

void UPhysicsMovementComponent::SetAutoTargetYawStrength(float Strength)
{
	NetworkPredictionProxy.ModifyNetState<FPhysicsMovementAsyncModelDef>([Strength](FPhysicsMovementNetState& NetState)
	{
		NetState.AutoFaceTargetYawStrength = Strength;
	});	
}

void UPhysicsMovementComponent::SetAutoTargetYawDamp(float YawDamp)
{
	NetworkPredictionProxy.ModifyNetState<FPhysicsMovementAsyncModelDef>([YawDamp](FPhysicsMovementNetState& NetState)
	{
		NetState.AutoFaceTargetYawDamp = YawDamp;
	});	
}

void UPhysicsMovementComponent::SetEnableTargetYaw(bool bTargetYaw)
{
	NetworkPredictionProxy.ModifyNetState<FPhysicsMovementAsyncModelDef>([bTargetYaw](FPhysicsMovementNetState& NetState)
	{
		NetState.bEnableAutoFaceTargetYaw = bTargetYaw;
	});
}


void UPhysicsMovementComponent::SetEnableKeepUpright(bool bKeepUpright)
{
	NetworkPredictionProxy.ModifyNetState<FPhysicsMovementAsyncModelDef>([bKeepUpright](FPhysicsMovementNetState& NetState)
	{
		NetState.bEnableKeepUpright = bKeepUpright;
	});
}

void UPhysicsMovementComponent::SetAutoBrakeStrength(float AutoBrakeStrength)
{
	NetworkPredictionProxy.ModifyNetState<FPhysicsMovementAsyncModelDef>([AutoBrakeStrength](FPhysicsMovementNetState& NetState)
	{
		NetState.AutoBrakeStrength = AutoBrakeStrength;
	});
}


void UPhysicsMovementComponent::TestMisprediction()
{
	const int32 RandValue = FMath::RandHelper(1024);
	UE_LOG(LogNetworkPhysics, Warning, TEXT("Setting NewRand on to %d"), RandValue);
	NetworkPredictionProxy.ModifyNetState<FPhysicsMovementAsyncModelDef>([RandValue](FPhysicsMovementNetState& NetState)
	{
		NetState.RandValue = RandValue;
	});	
}

FAutoConsoleCommandWithWorldAndArgs ForcePhysicsMovementCorrectionCmd(TEXT("np.PhysicsMovement.ForceRandCorrection2"), TEXT(""),
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
					if (UPhysicsMovementComponent* PhysComp = ActorIt->FindComponentByClass<UPhysicsMovementComponent>())
					{
						PhysComp->TestMisprediction();
					}
				}
			}
		}
	}));

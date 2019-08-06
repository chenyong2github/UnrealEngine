// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Movement/FlyingMovement.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/CharacterMovementComponent.h" // Temp
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Engine/LocalPlayer.h"
#include "Misc/CoreDelegates.h"
#include "UObject/CoreNet.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "UObject/UObjectIterator.h"
#include "Components/CapsuleComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "Math/Color.h"
#include "DrawDebugHelpers.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Debug/ReporterGraph.h"
#include "NetworkSimulationModelDebugger.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlyingMovement, Log, All);


namespace FlyingMovementCVars
{

static float PenetrationPullbackDistance = 0.125f;
static FAutoConsoleVariableRef CVarPenetrationPullbackDistance(TEXT("fp.PenetrationPullbackDistance"),
	PenetrationPullbackDistance,
	TEXT("Pull out from penetration of an object by this extra distance.\n")
	TEXT("Distance added to penetration fix-ups."),
	ECVF_Default);

static float PenetrationOverlapCheckInflation = 0.100f;
static FAutoConsoleVariableRef CVarPenetrationOverlapCheckInflation(TEXT("motion.PenetrationOverlapCheckInflation"),
	PenetrationOverlapCheckInflation,
	TEXT("Inflation added to object when checking if a location is free of blocking collision.\n")
	TEXT("Distance added to inflation in penetration overlap check."),
	ECVF_Default);

static int32 RequestMispredict = 0;
static FAutoConsoleVariableRef CVarRequestMispredict(TEXT("fp.RequestMispredict"),
	RequestMispredict, TEXT("Causes a misprediction by inserting random value into stream on authority side"), ECVF_Default);
}

// ----------------------------------------------------------------------------------------------------------
//	UFlyingMovementComponent setup/init
// ----------------------------------------------------------------------------------------------------------

UFlyingMovementComponent::UFlyingMovementComponent()
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bCanEverTick = true;
	
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	bReplicates = true;

	bWantsInitializeComponent = true;
}

void UFlyingMovementComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);

	// Super may start up the tick function when we don't want to.
	UpdateTickRegistration();

	// If the owner ticks, make sure we tick first. This is to ensure the owner's location will be up to date when it ticks.
	AActor* Owner = GetOwner();
	
	if (bRegister && PrimaryComponentTick.bCanEverTick && Owner && Owner->CanEverTick())
	{
		Owner->PrimaryActorTick.AddPrerequisite(this, PrimaryComponentTick);
	}
}

void UFlyingMovementComponent::UpdateTickRegistration()
{
	const bool bHasUpdatedComponent = (UpdatedComponent != NULL);
	SetComponentTickEnabled(bHasUpdatedComponent && bAutoActivate);
}

void UFlyingMovementComponent::InitializeComponent()
{
	TGuardValue<bool> InInitializeComponentGuard(bInInitializeComponent, true);
	Super::InitializeComponent();

	// RootComponent is null in OnRegister for blueprint (non-native) root components.
	if (!UpdatedComponent)
	{
		// Auto-register owner's root component if found.
		if (AActor* MyActor = GetOwner())
		{
			if (USceneComponent* NewUpdatedComponent = MyActor->GetRootComponent())
			{
				SetUpdatedComponent(NewUpdatedComponent);
			}
		}
	}
}

void UFlyingMovementComponent::OnRegister()
{
	TGuardValue<bool> InOnRegisterGuard(bInOnRegister, true);

	UpdatedPrimitive = Cast<UPrimitiveComponent>(UpdatedComponent);
	Super::OnRegister();

	const UWorld* MyWorld = GetWorld();
	if (MyWorld && MyWorld->IsGameWorld())
	{
		USceneComponent* NewUpdatedComponent = UpdatedComponent;
		if (!UpdatedComponent)
		{
			// Auto-register owner's root component if found.
			AActor* MyActor = GetOwner();
			if (MyActor)
			{
				NewUpdatedComponent = MyActor->GetRootComponent();
			}
		}

		SetUpdatedComponent(NewUpdatedComponent);
	}
}

void UFlyingMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	if (UpdatedComponent && UpdatedComponent != NewUpdatedComponent)
	{
		UpdatedComponent->SetShouldUpdatePhysicsVolume(false);
		if (!UpdatedComponent->IsPendingKill())
		{
			UpdatedComponent->SetPhysicsVolume(NULL, true);
			UpdatedComponent->PhysicsVolumeChangedDelegate.RemoveDynamic(this, &UFlyingMovementComponent::PhysicsVolumeChanged);
		}

		// remove from tick prerequisite
		UpdatedComponent->PrimaryComponentTick.RemovePrerequisite(this, PrimaryComponentTick); 
	}

	// Don't assign pending kill components, but allow those to null out previous UpdatedComponent.
	UpdatedComponent = IsValid(NewUpdatedComponent) ? NewUpdatedComponent : NULL;
	UpdatedPrimitive = Cast<UPrimitiveComponent>(UpdatedComponent);

	// Assign delegates
	if (UpdatedComponent && !UpdatedComponent->IsPendingKill())
	{
		UpdatedComponent->SetShouldUpdatePhysicsVolume(true);
		UpdatedComponent->PhysicsVolumeChangedDelegate.AddUniqueDynamic(this, &UFlyingMovementComponent::PhysicsVolumeChanged);

		if (!bInOnRegister && !bInInitializeComponent)
		{
			// UpdateOverlaps() in component registration will take care of this.
			UpdatedComponent->UpdatePhysicsVolume(true);
		}
		
		// force ticks after movement component updates
		UpdatedComponent->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick); 
	}
	UpdateTickRegistration();
}

void UFlyingMovementComponent::PhysicsVolumeChanged(APhysicsVolume* NewVolume)
{
	// This itself feels bad. When will this be called? Its impossible to know what is allowed and not allowed to be done in this callback.
	// Callbacks instead should be trapped within the simulation update function. This isn't really possible though since the UpdateComponent
	// is the one that will call this.
}

// ----------------------------------------------------------------------------------------------------------
//	Core Network Prediction functions
// ----------------------------------------------------------------------------------------------------------

IReplicationProxy* UFlyingMovementComponent::InstantiateNetworkSimulation()
{
	NetworkSim.Reset(new FlyingMovement::FMovementSystem());

#if NETSIM_MODEL_DEBUG
	FNetworkSimulationModelDebuggerManager::Get().RegisterNetworkSimulationModel(NetworkSim.Get(), (IFlyingMovementDriver*)this, GetOwner(), TEXT("FlyingPawnMovement"));
#endif
	return NetworkSim.Get();
}

	// Child classes should override this an initialize their NetworkSim here
void UFlyingMovementComponent::InitializeForNetworkRole(ENetRole Role)
{
	check(NetworkSim);

	FNetworkSimulationModelInitParameters InitParams;
	InitParams.InputBufferSize = Role != ROLE_SimulatedProxy ? 32 : 0;
	InitParams.SyncedBufferSize = Role != ROLE_AutonomousProxy ? 2 : 32;
	InitParams.AuxBufferSize = Role != ROLE_AutonomousProxy ? 2 : 32;
	InitParams.DebugBufferSize = 32;
	InitParams.HistoricBufferSize = 128;
	NetworkSim->InitializeForNetworkRole(Role, IsLocallyControlled(), InitParams);
}

void UFlyingMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// TEMP! Disable existing CMC if it is activate. Just makes A/B testing eaiser for now.
	if (AActor* Owner = GetOwner())
	{
		if (UCharacterMovementComponent* OldComp = Owner->FindComponentByClass<UCharacterMovementComponent>())
		{
			if (OldComp->IsActive())
			{
				OldComp->Deactivate();
				Owner->bReplicateMovement = false;
			}
		}
	}



	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ENetRole OwnerRole = GetOwnerRole();

	// -------------------------------------
	// Tick the network sim
	// -------------------------------------
	if (NetworkSim)
	{
		// Check if we should trip a mispredict. (Note how its not possible to do this inside the Update function!)
		if (OwnerRole == ROLE_Authority && FlyingMovementCVars::RequestMispredict)
		{
			FlyingMovement::FMovementSystem::ForceMispredict = true;
			FlyingMovementCVars::RequestMispredict = 0;
		}

		FlyingMovement::FMovementSystem::FTickParameters Parameters;
		Parameters.Role = OwnerRole;
		Parameters.LocalDeltaTimeSeconds = DeltaTime;

		// Tick the core network sim, this will consume input and generate new sync state
		NetworkSim->Tick((IFlyingMovementDriver*)this, Parameters);

		// Client->Server communication
		if (NetworkSim->ShouldSendServerRPC(OwnerRole, DeltaTime))
		{
			// Temp hack to make sure the ServerRPC doesn't get suppressed from bandwidth limiting
			// (system hasn't been optimized and not mature enough yet to handle gaps in input stream)
			FScopedBandwidthLimitBypass BandwidthBypass(GetOwner());

			FServerReplicationRPCParameter ProxyParameter(ReplicationProxy_ServerRPC);
			ServerRecieveClientInput(ProxyParameter);
		}
	}
}

FlyingMovement::FInputCmd* UFlyingMovementComponent::GetNextClientInputCmdForWrite(float DeltaTime)
{
	// This is unfortunate but we must check here to initialize the InputCmdBuffer.
	// -Tick alone is not appropriate since the PC may tick first and generate new user cmds (before our first tick)
	// -Startup functions (BeginPlay, PostInitializeComponent etc) are not appropriate because Net Role is not known.
	// -PostNetReceive could work but would be inefficient since we would be checking each simulated proxy too, ever PostNetReceive.
	//
	// This works out best because its only the locally controlled actor that will call into this.
	// Its just unfortunate it will only pass the first time but we must keep checking.
	CheckOwnerRoleChange();

	if (NetworkSim.IsValid())
	{
		return NetworkSim->GetNextInputForWrite(DeltaTime);
	}
	return nullptr;
}

// ----------------------------------------------------------------------------------------------------------
//	Movement System Driver
//
//	NOTE: Most of the Movement Driver is not ideal! We are at the mercy of the UpdateComponent since it is the
//	the object that owns its collision data and its MoveComponent function. Ideally we would have everything within
//	the movement simulation code and it do its own collision queries. But instead we have to come back to the Driver/Component
//	layer to do this kind of stuff.
//
// ----------------------------------------------------------------------------------------------------------

FVector UFlyingMovementComponent::GetPenetrationAdjustment(const FHitResult& Hit)
{
	if (!Hit.bStartPenetrating)
	{
		return FVector::ZeroVector;
	}

	FVector Result;
	const float PullBackDistance = FMath::Abs(FlyingMovementCVars::PenetrationPullbackDistance);
	const float PenetrationDepth = (Hit.PenetrationDepth > 0.f ? Hit.PenetrationDepth : 0.125f);

	Result = Hit.Normal * (PenetrationDepth + PullBackDistance);
	
	return Result;
}

bool UFlyingMovementComponent::OverlapTest(const FVector& Location, const FQuat& RotationQuat, const ECollisionChannel CollisionChannel, const FCollisionShape& CollisionShape, const AActor* IgnoreActor) const
{
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MovementOverlapTest), false, IgnoreActor);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(QueryParams, ResponseParam);
	return GetWorld()->OverlapBlockingTestByChannel(Location, RotationQuat, CollisionChannel, CollisionShape, QueryParams, ResponseParam);
}

void UFlyingMovementComponent::InitCollisionParams(FCollisionQueryParams &OutParams, FCollisionResponseParams& OutResponseParam) const
{
	if (UpdatedPrimitive)
	{
		UpdatedPrimitive->InitSweepCollisionParams(OutParams, OutResponseParam);
	}
}

bool UFlyingMovementComponent::ResolvePenetration(const FVector& ProposedAdjustment, const FHitResult& Hit, const FQuat& NewRotationQuat) const
{
	// SceneComponent can't be in penetration, so this function really only applies to PrimitiveComponent.
	const FVector Adjustment = ProposedAdjustment; //ConstrainDirectionToPlane(ProposedAdjustment);
	if (!Adjustment.IsZero() && UpdatedPrimitive)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FlyingMovementComponent_ResolvePenetration);
		// See if we can fit at the adjusted location without overlapping anything.
		AActor* ActorOwner = UpdatedComponent->GetOwner();
		if (!ActorOwner)
		{
			return false;
		}

		UE_LOG(LogFlyingMovement, Verbose, TEXT("ResolvePenetration: %s.%s at location %s inside %s.%s at location %s by %.3f (netmode: %d)"),
			   *ActorOwner->GetName(),
			   *UpdatedComponent->GetName(),
			   *UpdatedComponent->GetComponentLocation().ToString(),
			   *GetNameSafe(Hit.GetActor()),
			   *GetNameSafe(Hit.GetComponent()),
			   Hit.Component.IsValid() ? *Hit.GetComponent()->GetComponentLocation().ToString() : TEXT("<unknown>"),
			   Hit.PenetrationDepth,
			   (uint32)GetNetMode());

		// We really want to make sure that precision differences or differences between the overlap test and sweep tests don't put us into another overlap,
		// so make the overlap test a bit more restrictive.
		const float OverlapInflation = FlyingMovementCVars::PenetrationOverlapCheckInflation;
		bool bEncroached = OverlapTest(Hit.TraceStart + Adjustment, NewRotationQuat, UpdatedPrimitive->GetCollisionObjectType(), UpdatedPrimitive->GetCollisionShape(OverlapInflation), ActorOwner);
		if (!bEncroached)
		{
			// Move without sweeping.
			MoveUpdatedComponent(Adjustment, NewRotationQuat, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogFlyingMovement, Verbose, TEXT("ResolvePenetration:   teleport by %s"), *Adjustment.ToString());
			return true;
		}
		else
		{
			// Disable MOVECOMP_NeverIgnoreBlockingOverlaps if it is enabled, otherwise we wouldn't be able to sweep out of the object to fix the penetration.
			TGuardValue<EMoveComponentFlags> ScopedFlagRestore(MoveComponentFlags, EMoveComponentFlags(MoveComponentFlags & (~MOVECOMP_NeverIgnoreBlockingOverlaps)));

			// Try sweeping as far as possible...
			FHitResult SweepOutHit(1.f);
			bool bMoved = MoveUpdatedComponent(Adjustment, NewRotationQuat, true, &SweepOutHit, ETeleportType::TeleportPhysics);
			UE_LOG(LogFlyingMovement, Verbose, TEXT("ResolvePenetration:   sweep by %s (success = %d)"), *Adjustment.ToString(), bMoved);
			
			// Still stuck?
			if (!bMoved && SweepOutHit.bStartPenetrating)
			{
				// Combine two MTD results to get a new direction that gets out of multiple surfaces.
				const FVector SecondMTD = GetPenetrationAdjustment(SweepOutHit);
				const FVector CombinedMTD = Adjustment + SecondMTD;
				if (SecondMTD != Adjustment && !CombinedMTD.IsZero())
				{
					bMoved = MoveUpdatedComponent(CombinedMTD, NewRotationQuat, true, nullptr, ETeleportType::TeleportPhysics);
					UE_LOG(LogFlyingMovement, Verbose, TEXT("ResolvePenetration:   sweep by %s (MTD combo success = %d)"), *CombinedMTD.ToString(), bMoved);
				}
			}

			// Still stuck?
			if (!bMoved)
			{
				// Try moving the proposed adjustment plus the attempted move direction. This can sometimes get out of penetrations with multiple objects
				const FVector MoveDelta = (Hit.TraceEnd - Hit.TraceStart); //ConstrainDirectionToPlane(Hit.TraceEnd - Hit.TraceStart);
				if (!MoveDelta.IsZero())
				{
					bMoved = MoveUpdatedComponent(Adjustment + MoveDelta, NewRotationQuat, true, nullptr, ETeleportType::TeleportPhysics);
					UE_LOG(LogFlyingMovement, Verbose, TEXT("ResolvePenetration:   sweep by %s (adjusted attempt success = %d)"), *(Adjustment + MoveDelta).ToString(), bMoved);
				}
			}	

			return bMoved;
		}
	}

	return false;
}

bool UFlyingMovementComponent::SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport) const
{
	if (UpdatedComponent == NULL)
	{
		OutHit.Reset(1.f);
		return false;
	}

	bool bMoveResult = false;

	// Scope for move flags
	{
		bMoveResult = MoveUpdatedComponent(Delta, NewRotation, bSweep, &OutHit, Teleport);
	}

	// Handle initial penetrations
	if (OutHit.bStartPenetrating && UpdatedComponent)
	{
		const FVector RequestedAdjustment = GetPenetrationAdjustment(OutHit);
		if (ResolvePenetration(RequestedAdjustment, OutHit, NewRotation))
		{
			// Retry original move
			bMoveResult = MoveUpdatedComponent(Delta, NewRotation, bSweep, &OutHit, Teleport);
		}
	}

	return bMoveResult;
}

bool UFlyingMovementComponent::MoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport) const
{
	if (UpdatedComponent)
	{
		const FVector NewDelta = Delta; //ConstrainDirectionToPlane(Delta);
		return UpdatedComponent->MoveComponent(NewDelta, NewRotation, bSweep, OutHit, MoveComponentFlags, Teleport);
	}

	return false;
}

FTransform UFlyingMovementComponent::GetUpdateComponentTransform() const
{
	if (ensure(UpdatedComponent))
	{
		return UpdatedComponent->GetComponentTransform();		
	}
	return FTransform::Identity;
}

void UFlyingMovementComponent::InitSyncState(FlyingMovement::FMoveState& OutSyncState) const
{
	OutSyncState.Location = UpdatedComponent->GetComponentLocation();
	OutSyncState.Rotation = UpdatedComponent->GetComponentQuat().Rotator();	
}

void UFlyingMovementComponent::SyncTo(const FlyingMovement::FMoveState& SyncState)
{
	// Does checking equality make any sense here? This is unfortunate
	if (UpdatedComponent->GetComponentLocation().Equals(SyncState.Location) == false || UpdatedComponent->GetComponentQuat().Rotator().Equals(SyncState.Rotation, FlyingMovement::ROTATOR_TOLERANCE) == false)
	{
		FTransform Transform(SyncState.Rotation.Quaternion(), SyncState.Location, UpdatedComponent->GetComponentTransform().GetScale3D() );
		UpdatedComponent->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);

		UpdatedComponent->ComponentVelocity = SyncState.Velocity;
	}
}

void UFlyingMovementComponent::GetCapsuleDimensions(float &Radius, float& HalfHeight) const
{
	if (UCapsuleComponent const* CapsuleComponent = Cast<UCapsuleComponent>(UpdatedComponent))
	{
		CapsuleComponent->GetScaledCapsuleSize(Radius, HalfHeight);
	}
}

FTransform UFlyingMovementComponent::GetDebugWorldTransform() const
{
	return GetOwner()->GetActorTransform();
}

UObject* UFlyingMovementComponent::GetVLogOwner() const
{
	return GetOwner();
}
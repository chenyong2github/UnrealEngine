// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Movement/BaseMovementComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "VisualLogger/VisualLogger.h"
#include "NetworkPredictionTypes.h"


DEFINE_LOG_CATEGORY_STATIC(LogBaseMovement, Log, All);


namespace BaseMovementCVars
{

static float PenetrationPullbackDistance = 0.125f;
static FAutoConsoleVariableRef CVarPenetrationPullbackDistance(TEXT("bm.PenetrationPullbackDistance"),
	PenetrationPullbackDistance,
	TEXT("Pull out from penetration of an object by this extra distance.\n")
	TEXT("Distance added to penetration fix-ups."),
	ECVF_Default);

static float PenetrationOverlapCheckInflation = 0.100f;
static FAutoConsoleVariableRef CVarPenetrationOverlapCheckInflation(TEXT("bm.PenetrationOverlapCheckInflation"),
	PenetrationOverlapCheckInflation,
	TEXT("Inflation added to object when checking if a location is free of blocking collision.\n")
	TEXT("Distance added to inflation in penetration overlap check."),
	ECVF_Default);

static int32 RequestMispredict = 0;
static FAutoConsoleVariableRef CVarRequestMispredict(TEXT("bm.RequestMispredict"),
	RequestMispredict, TEXT("Causes a misprediction by inserting random value into stream on authority side"), ECVF_Default);


static int32 UseVLogger = 1;
static FAutoConsoleVariableRef CVarUseVLogger(TEXT("bm.Debug.UseUnrealVLogger"),
	UseVLogger,	TEXT("Use Unreal Visual Logger\n"),	ECVF_Default);

static int32 UseDrawDebug = 1;
static FAutoConsoleVariableRef CVarUseDrawDebug(TEXT("bm.Debug.UseDrawDebug"),
	UseVLogger,	TEXT("Use built in DrawDebug* functions for visual logging\n"), ECVF_Default);

static float DrawDebugDefaultLifeTime = 30.f;
static FAutoConsoleVariableRef CVarDrawDebugDefaultLifeTime(TEXT("bm.Debug.UseDrawDebug.DefaultLifeTime"),
	DrawDebugDefaultLifeTime, TEXT("Use built in DrawDebug* functions for visual logging"), ECVF_Default);
}

// ----------------------------------------------------------------------------------------------------------
//	UBaseMovementComponent setup/init
// ----------------------------------------------------------------------------------------------------------

UBaseMovementComponent::UBaseMovementComponent()
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bCanEverTick = true;
	
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	bReplicates = true;
	bWantsInitializeComponent = true;
}

void UBaseMovementComponent::RegisterComponentTickFunctions(bool bRegister)
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

void UBaseMovementComponent::UpdateTickRegistration()
{
	const bool bHasUpdatedComponent = (UpdatedComponent != NULL);
	SetComponentTickEnabled(bHasUpdatedComponent && bAutoActivate);
}

void UBaseMovementComponent::InitializeComponent()
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

void UBaseMovementComponent::OnRegister()
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

void UBaseMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	if (UpdatedComponent && UpdatedComponent != NewUpdatedComponent)
	{
		UpdatedComponent->SetShouldUpdatePhysicsVolume(false);
		if (!UpdatedComponent->IsPendingKill())
		{
			UpdatedComponent->SetPhysicsVolume(NULL, true);
			UpdatedComponent->PhysicsVolumeChangedDelegate.RemoveDynamic(this, &UBaseMovementComponent::PhysicsVolumeChanged);
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
		UpdatedComponent->PhysicsVolumeChangedDelegate.AddUniqueDynamic(this, &UBaseMovementComponent::PhysicsVolumeChanged);

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

void UBaseMovementComponent::PhysicsVolumeChanged(APhysicsVolume* NewVolume)
{
	// This itself feels bad. When will this be called? Its impossible to know what is allowed and not allowed to be done in this callback.
	// Callbacks instead should be trapped within the simulation update function. This isn't really possible though since the UpdateComponent
	// is the one that will call this.
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

FVector UBaseMovementComponent::GetPenetrationAdjustment(const FHitResult& Hit)
{
	if (!Hit.bStartPenetrating)
	{
		return FVector::ZeroVector;
	}

	FVector Result;
	const float PullBackDistance = FMath::Abs(BaseMovementCVars::PenetrationPullbackDistance);
	const float PenetrationDepth = (Hit.PenetrationDepth > 0.f ? Hit.PenetrationDepth : 0.125f);

	Result = Hit.Normal * (PenetrationDepth + PullBackDistance);
	
	return Result;
}

bool UBaseMovementComponent::OverlapTest(const FVector& Location, const FQuat& RotationQuat, const ECollisionChannel CollisionChannel, const FCollisionShape& CollisionShape, const AActor* IgnoreActor) const
{
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MovementOverlapTest), false, IgnoreActor);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(QueryParams, ResponseParam);
	return GetWorld()->OverlapBlockingTestByChannel(Location, RotationQuat, CollisionChannel, CollisionShape, QueryParams, ResponseParam);
}

void UBaseMovementComponent::InitCollisionParams(FCollisionQueryParams &OutParams, FCollisionResponseParams& OutResponseParam) const
{
	if (UpdatedPrimitive)
	{
		UpdatedPrimitive->InitSweepCollisionParams(OutParams, OutResponseParam);
	}
}

bool UBaseMovementComponent::ResolvePenetration(const FVector& ProposedAdjustment, const FHitResult& Hit, const FQuat& NewRotationQuat) const
{
	// SceneComponent can't be in penetration, so this function really only applies to PrimitiveComponent.
	const FVector Adjustment = ProposedAdjustment; //ConstrainDirectionToPlane(ProposedAdjustment);
	if (!Adjustment.IsZero() && UpdatedPrimitive)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_BaseMovementComponent_ResolvePenetration);
		// See if we can fit at the adjusted location without overlapping anything.
		AActor* ActorOwner = UpdatedComponent->GetOwner();
		if (!ActorOwner)
		{
			return false;
		}

		UE_LOG(LogBaseMovement, Verbose, TEXT("ResolvePenetration: %s.%s at location %s inside %s.%s at location %s by %.3f (netmode: %d)"),
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
		const float OverlapInflation = BaseMovementCVars::PenetrationOverlapCheckInflation;
		bool bEncroached = OverlapTest(Hit.TraceStart + Adjustment, NewRotationQuat, UpdatedPrimitive->GetCollisionObjectType(), UpdatedPrimitive->GetCollisionShape(OverlapInflation), ActorOwner);
		if (!bEncroached)
		{
			// Move without sweeping.
			MoveUpdatedComponent(Adjustment, NewRotationQuat, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogBaseMovement, Verbose, TEXT("ResolvePenetration:   teleport by %s"), *Adjustment.ToString());
			return true;
		}
		else
		{
			// Disable MOVECOMP_NeverIgnoreBlockingOverlaps if it is enabled, otherwise we wouldn't be able to sweep out of the object to fix the penetration.
			TGuardValue<EMoveComponentFlags> ScopedFlagRestore(MoveComponentFlags, EMoveComponentFlags(MoveComponentFlags & (~MOVECOMP_NeverIgnoreBlockingOverlaps)));

			// Try sweeping as far as possible...
			FHitResult SweepOutHit(1.f);
			bool bMoved = MoveUpdatedComponent(Adjustment, NewRotationQuat, true, &SweepOutHit, ETeleportType::TeleportPhysics);
			UE_LOG(LogBaseMovement, Verbose, TEXT("ResolvePenetration:   sweep by %s (success = %d)"), *Adjustment.ToString(), bMoved);
			
			// Still stuck?
			if (!bMoved && SweepOutHit.bStartPenetrating)
			{
				// Combine two MTD results to get a new direction that gets out of multiple surfaces.
				const FVector SecondMTD = GetPenetrationAdjustment(SweepOutHit);
				const FVector CombinedMTD = Adjustment + SecondMTD;
				if (SecondMTD != Adjustment && !CombinedMTD.IsZero())
				{
					bMoved = MoveUpdatedComponent(CombinedMTD, NewRotationQuat, true, nullptr, ETeleportType::TeleportPhysics);
					UE_LOG(LogBaseMovement, Verbose, TEXT("ResolvePenetration:   sweep by %s (MTD combo success = %d)"), *CombinedMTD.ToString(), bMoved);
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
					UE_LOG(LogBaseMovement, Verbose, TEXT("ResolvePenetration:   sweep by %s (adjusted attempt success = %d)"), *(Adjustment + MoveDelta).ToString(), bMoved);
				}
			}	

			return bMoved;
		}
	}

	return false;
}

bool UBaseMovementComponent::SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport) const
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

bool UBaseMovementComponent::MoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport) const
{
	if (UpdatedComponent)
	{
		const FVector NewDelta = Delta;
		return UpdatedComponent->MoveComponent(NewDelta, NewRotation, bSweep, OutHit, MoveComponentFlags, Teleport);
	}

	return false;
}

FTransform UBaseMovementComponent::GetUpdateComponentTransform() const
{
	if (ensure(UpdatedComponent))
	{
		return UpdatedComponent->GetComponentTransform();		
	}
	return FTransform::Identity;
}

UObject* UBaseMovementComponent::GetVLogOwner() const
{
	return GetOwner();
}

void UBaseMovementComponent::DrawDebug(const IBaseMovementDriver::FDrawDebugParams& Params) const
{
	if (!UpdatedComponent)
	{
		return;
	}

	AActor* Owner = GetOwner();

	const bool PersistentLines = (Params.Lifetime == EVisualLoggingLifetime::Persistent);
	const float LifetimeSeconds = PersistentLines ? 20.f : -1.f;

	if (Params.DrawType == EVisualLoggingDrawType::Crumb)
	{
		if (BaseMovementCVars::UseDrawDebug)
		{
			static float PointSize = 3.f;
			DrawDebugPoint(Params.DebugWorld, Params.Transform.GetLocation(), PointSize, Params.DrawColor, PersistentLines, LifetimeSeconds);
		}

		if (BaseMovementCVars::UseVLogger)
		{
			static float CrumbRadius = 3.f;
			UE_VLOG_LOCATION(Params.DebugLogOwner, LogBaseMovement, Log, Params.Transform.GetLocation(), CrumbRadius, Params.DrawColor, TEXT("%s"), *Params.InWorldText);
		}
	}
	else
	{
		// Full drawing. Trying to provide a generic implementation that will give the most accurate representation.
		// Still, subclasses may want to customize this
		if (UCapsuleComponent* CapsuleComponent = Cast<UCapsuleComponent>(UpdatedComponent))
		{
			float Radius=0.f;
			float HalfHeight=0.f;
			CapsuleComponent->GetScaledCapsuleSize(Radius, HalfHeight);

			if (BaseMovementCVars::UseDrawDebug)
			{
				static const float Thickness = 2.f;
				DrawDebugCapsule(Params.DebugWorld, Params.Transform.GetLocation(), HalfHeight, Radius, Params.Transform.GetRotation(), Params.DrawColor, PersistentLines, LifetimeSeconds, 0.f, Thickness);
			}

			if (BaseMovementCVars::UseVLogger)
			{
				FVector VLogPosition = Params.Transform.GetLocation();
				VLogPosition.Z -= HalfHeight;
				UE_VLOG_CAPSULE(Params.DebugLogOwner, LogBaseMovement, Log, VLogPosition, HalfHeight, Radius, Params.Transform.GetRotation(), Params.DrawColor, TEXT("%s"), *Params.InWorldText);
			}
		}
		else
		{
			// Generic Actor Bounds drawing
			FBox LocalSpaceBox = Owner->CalculateComponentsBoundingBoxInLocalSpace();

			if (BaseMovementCVars::UseDrawDebug)
			{
				static const float Thickness = 2.f;

				FVector ActorOrigin;
				FVector ActorExtent;
				LocalSpaceBox.GetCenterAndExtents(ActorOrigin, ActorExtent);
				ActorExtent *= Params.Transform.GetScale3D();
				DrawDebugBox(Params.DebugWorld, Params.Transform.GetLocation(), ActorExtent, Params.Transform.GetRotation(), Params.DrawColor, PersistentLines, LifetimeSeconds, 0, Thickness);
			}

			if (BaseMovementCVars::UseVLogger)
			{
				UE_VLOG_OBOX(Params.DebugLogOwner, LogBaseMovement, Log, LocalSpaceBox, Params.Transform.ToMatrixWithScale(), Params.DrawColor, TEXT("%s"), *Params.InWorldText);
			}
		}
	}

	if (BaseMovementCVars::UseVLogger)
	{
		UE_VLOG(Params.DebugLogOwner, LogBaseMovement, Log, TEXT("%s"), *Params.LogText);
	}
}

IBaseMovementDriver::FDrawDebugParams::FDrawDebugParams(const FVisualLoggingParameters& Parameters, IBaseMovementDriver* LogDriver)
{
	DebugWorld = LogDriver->GetDriverWorld();
	DebugLogOwner = LogDriver->GetVLogOwner();
	DrawType = (Parameters.Context == EVisualLoggingContext::OtherMispredicted || Parameters.Context == EVisualLoggingContext::OtherPredicted) ? EVisualLoggingDrawType::Crumb : EVisualLoggingDrawType::Full;
	Lifetime = Parameters.Lifetime;
	DrawColor = Parameters.GetDebugColor();	
}
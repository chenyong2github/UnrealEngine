// Copyright Epic Games, Inc. All Rights Reserved

#pragma once

#include "NetworkPredictionComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "WorldCollision.h"
#include "Components/SceneComponent.h"

#include "BaseMovementComponent.generated.h"

// This provides a base for movement related simulations: moving an "UpdatedComponent" around the world.
// There is no actual Update function provided here, it will be implemented by subclasses.
class NETWORKPREDICTION_API FBaseMovementSimulation
{
public:
	bool SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport) const;
	bool MoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport) const;
	FTransform GetUpdateComponentTransform() const;

	FVector GetPenetrationAdjustment(const FHitResult& Hit) const;

	bool OverlapTest(const FVector& Location, const FQuat& RotationQuat, const ECollisionChannel CollisionChannel, const FCollisionShape& CollisionShape, const AActor* IgnoreActor) const;
	void InitCollisionParams(FCollisionQueryParams &OutParams, FCollisionResponseParams& OutResponseParam) const;
	bool ResolvePenetration(const FVector& ProposedAdjustment, const FHitResult& Hit, const FQuat& NewRotationQuat) const;
	
	USceneComponent* UpdatedComponent = nullptr;
	UPrimitiveComponent* UpdatedPrimitive = nullptr;

	/**  Flags that control the behavior of calls to MoveComponent() on our UpdatedComponent. */
	mutable EMoveComponentFlags MoveComponentFlags = MOVECOMP_NoFlags; // Mutable because we sometimes need to disable these flags ::ResolvePenetration. Better way may be possible
};

// -------------------------------------------------------------------------------------------------------------------------------
//	Base component for movement. This essentially has the generic glue for selecting an UpdatedComponent and moving it along the world
//	It is abstract in that you still need to define which simulation the component runs (via ::InstantiateNetworkedSimulation)
// -------------------------------------------------------------------------------------------------------------------------------
UCLASS(Abstract)
class NETWORKPREDICTION_API UBaseMovementComponent : public UNetworkPredictionComponent
{
	GENERATED_BODY()

public:

	UBaseMovementComponent();

	virtual void InitializeComponent() override;
	virtual void OnRegister() override;
	virtual void RegisterComponentTickFunctions(bool bRegister) override;

protected:

	// Basic "Update Component/Ticking"
	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent);
	virtual void UpdateTickRegistration();

	UFUNCTION()
	virtual void PhysicsVolumeChanged(class APhysicsVolume* NewVolume);	

	UPROPERTY()
	USceneComponent* UpdatedComponent = nullptr;

	UPROPERTY()
	UPrimitiveComponent* UpdatedPrimitive = nullptr;

private:

	/** Transient flag indicating whether we are executing OnRegister(). */
	bool bInOnRegister = false;
	
	/** Transient flag indicating whether we are executing InitializeComponent(). */
	bool bInInitializeComponent = false;
};
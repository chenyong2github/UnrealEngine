// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved

#pragma once

#include "NetworkPredictionComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "WorldCollision.h"
#include "Components/SceneComponent.h"

#include "BaseMovementComponent.generated.h"

// -------------------------------------------------------------------------------------------------------------------------------
//	Base component for movement. This essentially has the generic glue for selecting an UpdatedComponent and moving it along the world
//	It is abstract in that you still need to define which simulation the component runs (via ::InstantiateNetworkSimulation)
// -------------------------------------------------------------------------------------------------------------------------------

// This is the base driver, to be used by the backing movement TNetworkedSimulationModel. 
// It is the basic interface for moving and updating a primitive component that is owned by the UNetworkPredictionComponent component.
// It is unfortunate and not ideal for perf that we have to go through a virtual function call to do collision queries. 
// It also creates an awkward "dual hierarchy" when trying to share code like this.
// A world where the simulation can do all the collision work and just push the final result back to the component layer would be much better.
class IBaseMovementDriver
{
public:
	virtual bool SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport) const = 0;
	virtual bool MoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport) const = 0;
	virtual FTransform GetUpdateComponentTransform() const = 0;

	virtual UWorld* GetBaseMovementDriverWorld() const = 0;
	virtual UObject* GetBaseMovementVLogOwner() const = 0;

	struct FDrawDebugParams
	{
		FDrawDebugParams() { }
		FDrawDebugParams(const struct FVisualLoggingParameters& Parameters, IBaseMovementDriver* LogDriver);

		UWorld* DebugWorld;		// World to draw stuff to (can be different than this->GetWorld() in PIE case)
		UObject* DebugLogOwner; // Object that should own any VLog entries
		
		EVisualLoggingDrawType DrawType;
		EVisualLoggingLifetime Lifetime;
		FColor DrawColor;

		FTransform Transform;	// Where to draw (important to understand: you don't draw your current state, you draw where the simulation says to draw)
		FString InWorldText;
		FString LogText;
	};
	
	// This is the final debug drawing function that the driver can implement. UBaseMovementComponent implements a generic one that will draw capsules or actor bounds.
	virtual void DrawDebug(const FDrawDebugParams& Params) const = 0;
};

UCLASS(Abstract)
class NETWORKPREDICTION_API UBaseMovementComponent : public UNetworkPredictionComponent, public IBaseMovementDriver
{
	GENERATED_BODY()

public:

	UBaseMovementComponent();

	virtual void InitializeComponent() override;
	virtual void OnRegister() override;
	virtual void RegisterComponentTickFunctions(bool bRegister) override;

	// IBaseMovementDriver
	bool SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport) const override final;
	bool MoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport) const override final;
	FTransform GetUpdateComponentTransform() const override final;
	
	UWorld* GetBaseMovementDriverWorld() const override final { return GetWorld(); }
	UObject* GetBaseMovementVLogOwner() const override final;	

	virtual void DrawDebug(const IBaseMovementDriver::FDrawDebugParams& Params) const override;

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

	static FVector GetPenetrationAdjustment(const FHitResult& Hit);

	bool OverlapTest(const FVector& Location, const FQuat& RotationQuat, const ECollisionChannel CollisionChannel, const FCollisionShape& CollisionShape, const AActor* IgnoreActor) const;
	void InitCollisionParams(FCollisionQueryParams &OutParams, FCollisionResponseParams& OutResponseParam) const;
	bool ResolvePenetration(const FVector& ProposedAdjustment, const FHitResult& Hit, const FQuat& NewRotationQuat) const;

	/**  Flags that control the behavior of calls to MoveComponent() on our UpdatedComponent. */
	mutable EMoveComponentFlags MoveComponentFlags = MOVECOMP_NoFlags; // Mutable because we sometimes need to disable these flags ::ResolvePenetration. Better way may be possible
};
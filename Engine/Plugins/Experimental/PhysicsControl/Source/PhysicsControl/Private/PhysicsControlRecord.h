// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "PhysicsControlData.h"

struct FConstraintInstance;
struct FBodyInstance;
class UMeshComponent;

/**
 * The basic state of a physics control, created for every control record.
 */
struct FPhysicsControlState
{
	FPhysicsControlState()
		: bEnabled(false), bPendingDestroy(false)
	{
	}

	/** Removes any constraint and resets the state */
	void Reset();

	TSharedPtr<FConstraintInstance> ConstraintInstance;

	bool bEnabled;
	bool bPendingDestroy;
};

/**
 * There will be a PhysicsControlRecord created at runtime for every Control that has been created
 */
struct FPhysicsControlRecord : public TSharedFromThis<FPhysicsControlRecord>
{
	FPhysicsControlRecord(const FPhysicsControl& InControl)
		: PhysicsControl(InControl)
	{
	}

	/** 
	 * Creates the constraint (if necessary) and stores it in the state. ConstraintDebugOwner is passed 
	 * to the constraint on creation. 
	 */
	FConstraintInstance* CreateConstraint(UObject* ConstraintDebugOwner);

	/** Ensures the constraint frame matches the control point in the record. */
	void UpdateConstraintControlPoint();

	/** Sets the control point to the center of mass of the child mesh (or to zero if that fails). */
	void ResetControlPoint();

	/** The configuration data */
	FPhysicsControl PhysicsControl;

	/** The instance/runtime state - instantiated and kept up to date (during the tick) with PhysicsControl. */
	FPhysicsControlState PhysicsControlState;
};

/**
 * There will be a PhysicsBodyModifier created at runtime for every BodyInstance involved in the component
 */
struct FPhysicsBodyModifier : public TSharedFromThis<FPhysicsBodyModifier>
{
	FPhysicsBodyModifier(
		TObjectPtr<UMeshComponent> InMeshComponent, 
		const FName&               InBoneName, 
		EPhysicsMovementType       InMovementType, 
		float                      InGravityMultiplier)
	: MeshComponent(InMeshComponent), BoneName(InBoneName)
	, MovementType(InMovementType), GravityMultiplier(InGravityMultiplier)
	, bPendingDestroy(false)
	{}

	/**  The mesh that will be modified. */
	TObjectPtr<UMeshComponent> MeshComponent;

	/** The name of the skeletal mesh bone or the name of the static mesh body that will be modified. */
	FName                      BoneName;

	EPhysicsMovementType       MovementType;
	float                      GravityMultiplier;

	bool bPendingDestroy;
};

/**
 * Used internally/only at runtime to cache skeletal transforms at the start of the tick, to avoid
 * calculating them separately for every control.
 */
struct FCachedSkeletalMeshData
{
public:
	FCachedSkeletalMeshData() : ReferenceCount(0) {}

public:
	struct FBoneData
	{
		FBoneData()
			: Position(FVector::ZeroVector), Orientation(FQuat::Identity)
			, Velocity(FVector::ZeroVector), AngularVelocity(FVector::ZeroVector) {}
		FBoneData(const FVector& InPosition, const FQuat& InOrientation)
			: Position(InPosition), Orientation(InOrientation),
			Velocity(FVector::ZeroVector), AngularVelocity(FVector::ZeroVector) {}

		/**
		 * Sets position and velocity, and calculates velocity if Dt > 0 (otherwise sets it to zero)
		 */
		void Update(const FVector& InPosition, const FQuat& InOrientation, float Dt);
		FTransform GetTM() const { return FTransform(Orientation, Position); }

		FVector Position;
		FQuat   Orientation;
		FVector Velocity;
		FVector AngularVelocity;
	};

public:

	/**
	 * The cached skeletal data, updated at the start of each tick
	 */
	TArray<FBoneData> BoneData;

	/**
	 * The component transform. This is only stored so we can detect teleports
	 */
	FTransform ComponentTM;

	/**
	 * Track when skeletal meshes are going to be used so this entry can be removed, and also so we
	 * can add a tick dependency
	 */
	int32 ReferenceCount;
};


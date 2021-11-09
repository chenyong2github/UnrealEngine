// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassCommonTypes.h"
#include "MassAIMovementTypes.h"
#include "ZoneGraphAnnotationTypes.h"
#include "ZoneGraphTypes.h"
#include "MassMovementFragments.h"
#include "MassAIMovementFragments.generated.h"

class UWorld;

struct FMassAvoidanceColliderInstance
{
	float Radius = 0.f;
	float Offset = 0.f;
};

enum class EMassColliderType : uint8
{
	Circle,
	Pill,
};

struct FMassCircleCollider
{
	FMassCircleCollider() = default;
	FMassCircleCollider(const float Radius) : Radius(Radius) {}
	float Radius = 0.f;
};

struct FMassPillCollider
{
	FMassPillCollider() = default;
	FMassPillCollider(const float Radius, const float HalfLength) : Radius(Radius), HalfLength(HalfLength) {}
	float Radius = 0.f;
	float HalfLength = 0.f;
};

/** Fragment holding data for avoidance coliders */
USTRUCT()
struct MASSAIMOVEMENT_API FMassAvoidanceColliderFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassAvoidanceColliderFragment()
	{
		Type = EMassColliderType::Circle;
		Data[0] = 0.f;
		Data[1] = 0.f;
	}

	FMassAvoidanceColliderFragment(const FMassCircleCollider& Circle)
	{
		Type = EMassColliderType::Circle;
		Data[0] = Circle.Radius;
		Data[1] = 0.f;
	}

	FMassAvoidanceColliderFragment(const FMassPillCollider& Pill)
	{
		Type = EMassColliderType::Pill;
		Data[0] = Pill.Radius;
		Data[1] = Pill.HalfLength;
	}
	
	FMassCircleCollider GetCircleCollider() const
	{
		check(Type == EMassColliderType::Circle);
		return FMassCircleCollider(Data[0]);
	}

	FMassPillCollider GetPillCollider() const
	{
		check(Type == EMassColliderType::Pill);
		return FMassPillCollider(Data[0], Data[1]);
	}

	float Data[2];
	EMassColliderType Type;
};

/** Cell location for dynamic obstacles */
USTRUCT()
struct MASSAIMOVEMENT_API FMassAvoidanceObstacleGridCellLocationFragment : public FMassFragment
{
	GENERATED_BODY()
	FAvoidanceObstacleHashGrid2D::FCellLocation CellLoc;
};

/** Move target. */
USTRUCT()
struct MASSAIMOVEMENT_API FMassMoveTargetFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassMoveTargetFragment() : bNetDirty(false), bOffBoundaries(false), bSteeringFallingBehind(false) {}

	/** To setup current action from the authoritative world */
	void CreateNewAction(const EMassMovementAction InAction, const UWorld& InWorld);

	/** To setup current action from replicated data */
	void CreateReplicatedAction(const EMassMovementAction InAction, const uint16 InActionID, const float InWorldStartTime, const float InServerStartTime);

	void MarkNetDirty() { bNetDirty = true; }
	bool GetNetDirty() const { return bNetDirty; }
	void ResetNetDirty() { bNetDirty = false; }

public:
	FString ToString() const;

	EMassMovementAction GetPreviousAction() const { return PreviousAction; }
	EMassMovementAction GetCurrentAction() const { return CurrentAction; }
	float GetCurrentActionStartTime() const { return CurrentActionWorldStartTime; }
	float GetCurrentActionServerStartTime() const { return CurrentActionServerStartTime; }
	uint16 GetCurrentActionID() const { return CurrentActionID; }

	/** Center of the move target. */
	FVector Center = FVector::ZeroVector;

	/** Forward direction of the movement target.  */
	FVector Forward = FVector::ZeroVector;

	/** Distance remaining to the movement goal. */
	float DistanceToGoal = 0.0f;

	/** Allowed deviation around the movement target. */
	float SlackRadius = 0.0f;

private:
	/** World time in seconds when the action started. */
	float CurrentActionWorldStartTime = 0.0f;

	/** Server time in seconds when the action started. */
	float CurrentActionServerStartTime = 0.0f;

	/** Number incremented each time new action (i.e move, stand, animation) is started. */
	uint16 CurrentActionID = 0;

public:
	/** Requested movement speed. */
	FMassInt16Real DesiredSpeed = FMassInt16Real(0.0f);

	/** Intended movement action at the target. */
	EMassMovementAction IntentAtGoal = EMassMovementAction::Move;

private:
	/** Current movement action. */
	EMassMovementAction CurrentAction = EMassMovementAction::Move;

	/** Previous movement action. */
	EMassMovementAction PreviousAction = EMassMovementAction::Move;

	uint8 bNetDirty : 1;
public:
	/** True if the movement target is assumed to be outside navigation boundaries. */
	uint8 bOffBoundaries : 1;

	/** True if the movement target is assumed to be outside navigation boundaries. */
	uint8 bSteeringFallingBehind : 1;
};

/** Steering fragment. */
USTRUCT()
struct MASSAIMOVEMENT_API FMassSteeringFragment : public FMassFragment
{
	GENERATED_BODY()

	void Reset()
	{
		DesiredVelocity = FVector::ZeroVector;
		SteeringForce = FVector::ZeroVector;
	}

	/** Cached desired velocity from steering. Note: not used for moving the entity. */
	FVector DesiredVelocity = FVector::ZeroVector;

	/** Combined steering force from all steering, used to move the entity. */
	FVector SteeringForce = FVector::ZeroVector;
};

/** Steering ghost fragment. */
USTRUCT()
struct MASSAIMOVEMENT_API FMassSteeringGhostFragment : public FMassFragment
{
	GENERATED_BODY()

	bool IsValid(const uint16 CurrentActionID) const
	{
		return LastSeenActionID == CurrentActionID;
	}

	/** The action ID the ghost was initialized for */
	uint16 LastSeenActionID = 0;

	/** Location of the ghost */
	FVector Location = FVector::ZeroVector;
	
	/** Velocity of the ghost */
	FVector Velocity = FVector::ZeroVector;

	/** Selected steer target based on ghost, updates periodically. */
	FVector SteerTarget = FVector::ZeroVector;

	/** Used during target update to see when the target movement stops */
	float TargetMaxSpeed = 0.0f;

	/** Cooldown between target updates */
	float TargetCooldown = 0.0f;

	/** True if the target is being updated */
	bool bUpdatingTarget = false;

	/** True if we just entered from move action */
	bool bEnteredFromMoveAction = false;
};

USTRUCT()
struct MASSAIMOVEMENT_API FMassDynamicObstacleFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Lanes currently blocked by the dynamic obstacle. */
	static constexpr int MaxLaneObstacles = 8;
	TArray<FMassLaneObstacleID, TFixedAllocator<MaxLaneObstacles>> LaneObstacleIDs;

	/** Time stamp when that obstacle stopped moving. */
	FDateTime LastMovedTimeStamp;

	/** Position of the dynamic obstacle when it last moved. */
	FVector LastPosition = FVector::ZeroVector;

	/** Has this dynamic obstacle stopped moving. */
	bool bHasStopped = true;
};

/** Stores handle to rich movement config */
USTRUCT()
struct MASSAIMOVEMENT_API FMassMovementConfigFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Index to FMassMovementConfig in UMassMovementSettings. */
	FMassMovementConfigHandle ConfigHandle;
};

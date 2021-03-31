// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "IKRigDataTypes.generated.h"

struct FIKRigEffectorGoal;
struct FIKRigHierarchy;


USTRUCT(Blueprintable)
struct IKRIG_API FIKRigGoal
{
	GENERATED_BODY()
	
	/** Name of the IK Goal. Must correspond to the name of a Goal in the target IKRig asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FIKRigGoal)
	FName Name;

	/** Position of the IK goal in Component Space of target actor component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FIKRigGoal)
	FVector Position;

	/** Rotation of the IK goal in Component Space of target actor component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FIKRigGoal)
	FRotator Rotation;

	/** Range 0-1. Smoothly blends the Goal position from the input pose (0.0) to the Goal position (1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FIKRigGoal, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float PositionAlpha;

	/** Range 0-1. Smoothly blends the Goal rotation from the input pose (0.0) to the Goal rotation (1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FIKRigGoal, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float RotationAlpha;

	FIKRigGoal()
    : Name(NAME_None),
	Position(ForceInitToZero),
    Rotation(FRotator::ZeroRotator),
    PositionAlpha(1.f),
    RotationAlpha(0.0f){}
	
	FIKRigGoal(const FName& GoalName)
    : Name(GoalName),
	Position(ForceInitToZero),
    Rotation(FRotator::ZeroRotator),
    PositionAlpha(1.f),
    RotationAlpha(0.0f){}

	FIKRigGoal(
        const FName& Name,
        const FVector& Position,
        const FQuat& Rotation,
        const float PositionAlpha,
        const float RotationAlpha)
        : Name(Name),
          Position(Position),
          Rotation(Rotation.Rotator()),
          PositionAlpha(PositionAlpha),
          RotationAlpha(RotationAlpha){}

	FString ToString() const
	{
		return FString::Printf(TEXT("Name=%s, Pos=(%s, Alpha=%3.3f), Rot=(%s, Alpha=%3.3f)"),
			*Name.ToString(), *Position.ToString(), PositionAlpha, *Rotation.ToString(), RotationAlpha);
	}
};

inline uint32 GetTypeHash(FIKRigGoal ObjectRef) { return GetTypeHash(ObjectRef.Name); }

USTRUCT()
struct IKRIG_API FIKRigGoalContainer
{
	GENERATED_BODY()

public:
	
	/** Pre-load all the names of goals (optional, you can just call SetIKGoal to add as needed) */
	void InitializeGoalsFromNames(const TArray<FIKRigEffectorGoal>& InGoalNames);

	/** Set an IK goal to go to a specific location and rotation (in component space) blended by alpha.
	 * Will ADD the goal if none exist with the input name. */
	void SetIKGoal(const FIKRigGoal& InGoal);

	/** Get an IK goal with the given name. Returns false if no goal is found in the container with the name. */
	bool GetGoalByName(const FName& InGoalName, FIKRigGoal& OutGoal) const;

	/** Keys are IK Rig Goal names. Values are the Goal data structures.
	 * These are consumed by an IKRig asset to drive effectors. */
	TMap<FName, FIKRigGoal> Goals;
};
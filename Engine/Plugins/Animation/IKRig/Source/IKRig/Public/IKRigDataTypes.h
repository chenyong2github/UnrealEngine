// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IKRigDefinition.h"
#include "IKRigDataTypes.generated.h"

class UIKRigEffectorGoal;

UENUM(BlueprintType)
enum class EIKRigGoalSpace : uint8
{
	Component		UMETA(DisplayName = "Component Space"),
	Additive		UMETA(DisplayName = "Additive"),
    World			UMETA(DisplayName = "World Space"),
};

UENUM(BlueprintType)
enum class EIKRigGoalTransformSource : uint8
{
	Manual			UMETA(DisplayName = "Manual Input"),
    Bone			UMETA(DisplayName = "Bone"),
    ActorComponent	UMETA(DisplayName = "Actor Component"),
};

USTRUCT(Blueprintable)
struct IKRIG_API FIKRigGoal
{
	GENERATED_BODY()
	
	/** Name of the IK Goal. Must correspond to the name of a Goal in the target IKRig asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Goal)
	FName Name;

	/** Set the source of the transform data for the Goal.
	 * "Manual Input" uses the values provided by the blueprint node pin.
	 * "Bone" uses the transform of the bone provided by OptionalSourceBone.
	 * "Actor Component" uses the transform supplied by any Actor Components that implements the IIKGoalCreatorInterface*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Goal)
	EIKRigGoalTransformSource TransformSource = EIKRigGoalTransformSource::Manual;

	/** When TransformSource is set to "Bone" mode, the Position and Rotation will be driven by this Bone's input transform.
	 * When using a Bone as the transform source, the Position and Rotation Alpha values can still be set as desired.
	 * But the PositionSpace and RotationSpace are no longer relevant and will not be used.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Goal, meta = (CustomWidget = "BoneName"))
	FName OptionalSourceBone;

	/** Position of the IK goal in Component Space of target actor component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Position, meta=(EditCondition="Source == EIKRigGoalSource::Manual"))
	FVector Position;

	/** Rotation of the IK goal in Component Space of target actor component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Rotation, meta=(EditCondition="Source == EIKRigGoalSource::Manual"))
	FRotator Rotation;

	/** Range 0-1. Smoothly blends the Goal position from the input pose (0.0) to the Goal position (1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Position, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float PositionAlpha;

	/** Range 0-1. Smoothly blends the Goal rotation from the input pose (0.0) to the Goal rotation (1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Rotation, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float RotationAlpha;

	/** The space that the goal position is in.
	 *"Additive" treats the goal transform as an additive offset relative to the Bone at the effector.
	 *"Component" treats the goal transform as being in the space of the Skeletal Mesh Actor Component.
	 *"World" treats the goal transform as being in the space of the World. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Position)
	EIKRigGoalSpace PositionSpace;
	
	/** The space that the goal rotation is in.
	*"Additive" treats the goal transform as an additive offset relative to the Bone at the effector.
	*"Component" treats the goal transform as being in the space of the Skeletal Mesh Actor Component.
	*"World" treats the goal transform as being in the space of the World. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Rotation)
	EIKRigGoalSpace RotationSpace;

	UPROPERTY(Transient)
	FVector FinalBlendedPosition;

	UPROPERTY(Transient)
	FQuat FinalBlendedRotation;

	FIKRigGoal()
    : Name(NAME_None),
	Position(ForceInitToZero),
	Rotation(FRotator::ZeroRotator),
	PositionAlpha(0.0f),
    RotationAlpha(0.0f),
	PositionSpace(EIKRigGoalSpace::Component),
	RotationSpace(EIKRigGoalSpace::Component),
	FinalBlendedPosition(ForceInitToZero),
	FinalBlendedRotation(FQuat::Identity){}
	
	FIKRigGoal(const FName& GoalName)
    : Name(GoalName),
	Position(ForceInitToZero),
	Rotation(FRotator::ZeroRotator),
    PositionAlpha(0.0f),
    RotationAlpha(0.0f),
	PositionSpace(EIKRigGoalSpace::Component),
    RotationSpace(EIKRigGoalSpace::Component),
	FinalBlendedPosition(ForceInitToZero),
    FinalBlendedRotation(FQuat::Identity){}

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
        RotationAlpha(RotationAlpha),
		PositionSpace(EIKRigGoalSpace::Component),
		RotationSpace(EIKRigGoalSpace::Component),
		FinalBlendedPosition(Position),
		FinalBlendedRotation(Rotation){}

	FIKRigGoal(const UIKRigEffectorGoal* InGoal)
		: Name(InGoal->GoalName),
		Position(InGoal->CurrentTransform.GetTranslation()),
		Rotation(InGoal->CurrentTransform.Rotator()),
		PositionAlpha(InGoal->PositionAlpha),
        RotationAlpha(InGoal->RotationAlpha),
		PositionSpace(EIKRigGoalSpace::Component),
        RotationSpace(EIKRigGoalSpace::Component),
        FinalBlendedPosition(ForceInitToZero),
        FinalBlendedRotation(FQuat::Identity){}

	FString ToString() const
	{
		return FString::Printf(TEXT("Name=%s, Pos=(%s, Alpha=%3.3f), Rot=(%s, Alpha=%3.3f)"),
			*Name.ToString(), *FinalBlendedPosition.ToString(), PositionAlpha, *FinalBlendedRotation.ToString(), RotationAlpha);
	}
};

inline uint32 GetTypeHash(FIKRigGoal ObjectRef) { return GetTypeHash(ObjectRef.Name); }

USTRUCT()
struct IKRIG_API FIKRigGoalContainer
{
	GENERATED_BODY()

public:
	
	/** Pre-load all the names of goals (optional, you can just call SetIKGoal to add as needed) */
	void InitializeFromGoals(const TArray<UIKRigEffectorGoal*>& InGoals);

	/** Set an IK goal to go to a specific location and rotation (in component space) blended by alpha.
	 * Will ADD the goal if none exist with the input name. */
	void SetIKGoal(const FIKRigGoal& InGoal);

	/** Set an IK goal to go to a specific location and rotation (in component space) blended by alpha.
	* Will ADD the goal if none exist with the input name. */
	void SetIKGoal(const UIKRigEffectorGoal* InEffectorGoal);

	/** Get an IK goal with the given name. Returns false if no goal is found in the container with the name. */
	bool GetGoalByName(const FName& InGoalName, FIKRigGoal& OutGoal) const;

	/** Keys are IK Rig Goal names. Values are the Goal data structures.
	 * These are consumed by an IKRig asset to drive effectors. */
	TMap<FName, FIKRigGoal> Goals;
};

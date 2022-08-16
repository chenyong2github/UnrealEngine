// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoneControllers/BoneControllerTypes.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Animation/AnimNodeBase.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/EngineTypes.h"

#include "AnimNode_FootPlacement.generated.h"

namespace UE::Anim::FootPlacement
{
	enum class EPlantType
	{
		Unplanted,
		Planted,
		Replanted
	};

	struct FLegRuntimeData
	{
		int32 Idx = -1;

		struct FBoneData
		{
			FCompactPoseBoneIndex FKIndex = FCompactPoseBoneIndex(INDEX_NONE);
			FCompactPoseBoneIndex BallIndex = FCompactPoseBoneIndex(INDEX_NONE);
			FCompactPoseBoneIndex IKIndex = FCompactPoseBoneIndex(INDEX_NONE);
			FCompactPoseBoneIndex HipIndex = FCompactPoseBoneIndex(INDEX_NONE);
			float LimbLength = 0.0f;
			float FootLength = 0.0f;
		} Bones;

		SmartName::UID_Type SpeedCurveUID = SmartName::MaxUID;

		// Helper struct to store values used across the evaluation
		struct FInputPoseData
		{
			FTransform IKTransformCS = FTransform::Identity;
			FTransform FKTransformCS = FTransform::Identity;
			FTransform BallTransformCS = FTransform::Identity;
			FTransform HipTransformCS = FTransform::Identity;
			FTransform BallToFoot = FTransform::Identity;
			FTransform FootToBall = FTransform::Identity;
			FTransform FootToGround = FTransform::Identity;
			FTransform BallToGround = FTransform::Identity;
			float Speed = 0.0f;
			float DistanceToPlant = 0.0f;
			float AlignmentAlpha = 0.0f;
		} InputPose;

		/* Ground */

		struct FPlantData
		{
			UE::Anim::FootPlacement::EPlantType PlantType = UE::Anim::FootPlacement::EPlantType::Unplanted;
			UE::Anim::FootPlacement::EPlantType LastPlantType = UE::Anim::FootPlacement::EPlantType::Unplanted;
			FPlane PlantPlaneWS = FPlane(FVector::UpVector, 0.0f);
			FPlane PlantPlaneCS = FPlane(FVector::UpVector, 0.0f);
			FQuat TwistCorrection = FQuat::Identity;
			float TimeSinceFullyUnaligned = 0.0f;
			bool bCanReachTarget = false;
			// Whether we want to plant, independently from any dynamic pose adjustments we may do
			bool bWantsToPlant = false;
		} Plant;
		
		FTransform AlignedFootTransformWS = FTransform::Identity;
		FTransform UnalignedFootTransformWS = FTransform::Identity;
		FTransform AlignedFootTransformCS = FTransform::Identity;
		FVector CachedIKToFKDir = FVector::UpVector;
		
		/* Interpolation */
		struct FInterpolationData
		{
			FTransform UnalignedFootOffsetCS = FTransform::Identity;

			FVectorSpringState PlantOffsetTranslationSpringState;
			FQuaternionSpringState PlantOffsetRotationSpringState;

			FVectorSpringState GroundTranslationSpringState;
			FQuaternionSpringState GroundRotationSpringState;
		} Interpolation;
	};

	struct FPlantRuntimeSettings
	{
		float MaxExtensionRatioSqrd = 0.0f;
		float MinExtensionRatioSqrd = 0.0f;
		float MaxLinearErrorSqrd = 0.0f;
		float ReplantMaxLinearErrorSqrd = 0.0f;
		float CosHalfMaxRotationError = 0.0f;
		float CosHalfReplantMaxRotationError = 0.0f;
	};

	struct FPelvisRuntimeData
	{
		/* Bone IDs */
		struct FBones
		{
			FCompactPoseBoneIndex FkBoneIndex = FCompactPoseBoneIndex(INDEX_NONE);
			FCompactPoseBoneIndex IkBoneIndex = FCompactPoseBoneIndex(INDEX_NONE);
		} Bones;

		/* Settings-based properties */
		float MaxOffsetHorizontalSqrd = 0.0f;
		float MaxOffsetSqrd = 0.0f;

		/* Input pose properties */
		struct FInputPoseData
		{
			FTransform FKTransformCS = FTransform::Identity;
			FTransform IKRootTransformCS = FTransform::Identity;
		} InputPose;

		/* Interpolation */
		struct FInterpolationData
		{
			FVector PelvisTranslationOffset;
			FVectorSpringState PelvisTranslationSpringState;
		} Interpolation;
	};

	struct FCharacterData
	{
		FVector ComponentLocationWS = FVector::ZeroVector;
		int NumFKPlanted = 0;
		bool bIsOnGround = false;
	};

	struct FPlantResult
	{
	public:
		FBoneTransform IkPlantTranformCS;
		//FBoneTransform FkTipTransformCS;
		//FBoneTransform FkHipTransformCS;
	};

#if ENABLE_ANIM_DEBUG
	struct FDebugData
	{
		FVector OutputPelvisLocationWS = FVector::ZeroVector;
		FVector InputPelvisLocationWS = FVector::ZeroVector;

		TArray<FVector> OutputFootLocationsWS;
		TArray<FVector> InputFootLocationsWS;

		struct FLegExtension
		{
			float HyperExtensionAmount;
			float RollAmount;
			float PullAmount;
		};
		TArray<FLegExtension> LegsExtension;

		void Init(const int32 InSize)
		{
			OutputFootLocationsWS.SetNumUninitialized(InSize);
			InputFootLocationsWS.SetNumUninitialized(InSize);
			LegsExtension.SetNumUninitialized(InSize);
		}
	};
#endif

	struct FEvaluationContext;
}

UENUM(BlueprintType)
enum class EFootPlacementLockType : uint8
{
	Unlocked,
	PivotAroundBall,
	PivotAroundAnkle,
	LockRotation
};

USTRUCT(BlueprintType)
struct FFootPlacementInterpolationSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float UnplantLinearStiffness = 250.0f;

	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float UnplantLinearDamping = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float UnplantAngularStiffness = 450.0f;

	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float UnplantAngularDamping = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float FloorLinearStiffness = 1000.0f;

	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float FloorLinearDamping = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float FloorAngularStiffness = 450.0f;

	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float FloorAngularDamping = 1.0f;
};

USTRUCT(BlueprintType)
struct FFootPlacementTraceSettings
{
	GENERATED_BODY()

public:
	// TODO: implement?
	// Is tracing enabled?
	UPROPERTY(EditAnywhere, Category = "Trace Settings")
	bool bEnabled = true;

	// A negative value extends the trace length above the bone
	UPROPERTY(EditAnywhere, Category = "Trace Settings", meta=(EditCondition="bEnabled", DisplayAfter="bEnabled"))
	float StartOffset = -75.0f;

	// A positive value extends the trace length below the bone
	UPROPERTY(EditAnywhere, Category = "Trace Settings", meta=(EditCondition="bEnabled", DisplayAfter="bEnabled"))
	float EndOffset = 100.0f;

	// The trace is a sphere sweep with this radius. It should be big enough to prevent the trace from going through 
	// small geometry gaps
	UPROPERTY(EditAnywhere, Category = "Trace Settings", meta=(EditCondition="bEnabled", DisplayAfter="bEnabled"))
	float SweepRadius = 5.0f;

	// The channel to use for our complex trace
	UPROPERTY(EditAnywhere, Category = "Trace Settings", meta=(EditCondition="bEnabled", DisplayAfter="bEnabled"))
	TEnumAsByte<ETraceTypeQuery> ComplexTraceChannel = TraceTypeQuery1;

	// How much we align to simple vs complex collision when the foot is in flight
	// Tracing against simple geometry (i.e. it's common for stairs to have simplified ramp collisions) can provide a 
	// smoother trajectory when the foot is in flight
	UPROPERTY(EditAnywhere, Category = "Trace Settings", meta = (EditCondition = "bEnabled", DisplayAfter = "bEnabled"))
	float SimpleCollisionInfluence = 0.5f;

	// The channel to use for our simple trace
	UPROPERTY(EditAnywhere, Category = "Trace Settings", meta = (EditCondition = "bEnabled", DisplayAfter = "bEnabled"))
	TEnumAsByte<ETraceTypeQuery> SimpleTraceChannel = TraceTypeQuery1;
};

USTRUCT()
struct FFootPlacementRootDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Pelvis Settings")
	FBoneReference PelvisBone;

	UPROPERTY(EditAnywhere, Category = "Pelvis Settings")
	FBoneReference IKRootBone;

public:
	void Initialize(const FAnimationInitializeContext& Context);
};


USTRUCT(BlueprintType)
struct FFootPlacementPelvisSettings
{
	GENERATED_BODY()

public:
	// Max horizontal foot adjustment we consider to lower the hips
	// This can be used to prevent the hips from dropping too low when the feet are locked
	// Exceeding this value will first attempt to roll the planted feet, and then slide
	UPROPERTY(EditAnywhere, Category = "Pelvis Settings")
	float MaxOffsetHorizontal = 20.0f;

	// Max vertical offset from the input pose for the Pelvis.
	// Reaching this limit means the feet may not reach their plant plane
	UPROPERTY(EditAnywhere, Category = "Pelvis Settings")
	float MaxOffset = 50.0f;

	UPROPERTY(EditAnywhere, Category = "Pelvis Settings")
	float LinearStiffness = 350.0f;

	UPROPERTY(EditAnywhere, Category = "Pelvis Settings")
	float LinearDamping = 1.0f;

	// This is used to hold the Pelvis's interpolator in a fixed spot when the capsule suddenly moves (i.e. on a big step)
	// If your camera is directly attached to the character with little to no smoothing, you may want this disabled
	UPROPERTY(EditAnywhere, Category = "Pelvis Settings")
	bool bCompensateForSuddenCapsuleMoves = true;
};

USTRUCT()
struct FFootPlacemenLegDefinition
{
	GENERATED_BODY()

public:
	// Bone to be planted. For feet, use the heel/ankle joint.
	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference FKFootBone;

	// TODO: can we optionally output as an attribute?
	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference IKFootBone;

	// Secondary plant bone. For feet, use the ball joint.
	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference BallBone;

	UPROPERTY(EditAnywhere, Category = "Settings")
	int32 NumBonesInLimb = 2;

	// Name of the curve representing the foot/ball speed. Not required in Graph speed mode
	UPROPERTY(EditAnywhere, Category = "Settings")
	FName SpeedCurveName = NAME_None;

public:

	void InitializeBoneReferences(const FBoneContainer& RequiredBones);
};

USTRUCT(BlueprintType)
struct FFootPlacementPlantSettings
{
	GENERATED_BODY()

public:
	
	// At this distance from the planting plane the bone is considered planted and will be fully aligned.
	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float FKPlantDistance = 10.0f;

	// Bone is considered planted below this speed. Value is obtained from FKSpeedCurveName
	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float SpeedThreshold = 60.0f;

	// Max extension ratio of the chain, calculated from the remaining length between current pose and full limb extension
	UPROPERTY(EditAnywhere, Category = "Plant Settings", meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	float MaxExtensionRatio = 0.5f;

	// Min extension ratio of the chain, calculated from the total limb length, and adjusted along the approach direction
	UPROPERTY(EditAnywhere, Category = "Plant Settings", meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	float MinExtensionRatio = 0.2f;

	// How much linear deviation causes the constraint to be released
	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float MaxLinearError = 35.0f;

	// Below this value, proportional to MaxLinearError, the bone will replant
	UPROPERTY(EditAnywhere, Category = "Plant Settings", meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	float ReplantMaxLinearErrorRatio = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	EFootPlacementLockType LockType = EFootPlacementLockType::PivotAroundBall;

	// How much angular deviation (in degrees) causes the constraint to be released for replant
	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float MaxRotationError = 45.0;

	// Below this value, proportional to MaxRotationError, the bone will replant
	UPROPERTY(EditAnywhere, Category = "Plant Settings", meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	float ReplantMaxRotationErrorRatio = 0.5f;

	// Speed at which we transition to fully unplanted.
	// The range between SpeedThreshold and UnalignmentSpeedThreshold should roughly represent the roll-phase of the foot
	// TODO: This feels innaccurate most of the time, and varies depending on anim speed. Improve this
	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float UnalignmentSpeedThreshold = 200.0f;

	// How much we reduce the procedural ankle twist adjustment used to align the foot to the ground slope.
	UPROPERTY(EditAnywhere, Category = "Plant Settings")
	float AnkleTwistReduction = 0.75f;

	// How much we can pull the the foot towards the hip to prevent hyperextension
	// Before pulling the IK foot towards the FK foot
	// While we're planted and within this threshold, the foot will roll instead of sliding
	UPROPERTY(EditAnywhere, Category = "Settings")
	float ExtensionPlantedPullOffset = 2.0f;

	void Initialize(const FAnimationInitializeContext& Context);
};


USTRUCT(BlueprintInternalUseOnly, Experimental)
struct ANIMATIONWARPINGRUNTIME_API FAnimNode_FootPlacement : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

public:

	// Foot/Ball speed evaluation mode (Graph or Manual) used to decide when the feet are locked
	// Graph mode uses the root motion attribute from the animations to calculate the joint's speed
	// Manual mode uses a per-foot curve name representing the joint's speed
	UPROPERTY(EditAnywhere, Category = "Settings")
	EWarpingEvaluationMode PlantSpeedMode = EWarpingEvaluationMode::Manual;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FFootPlacemenLegDefinition> LegDefinitions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plant", meta = (PinHiddenByDefault))
	FFootPlacementPlantSettings PlantSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interpolation", meta = (PinHiddenByDefault))
	FFootPlacementInterpolationSettings InterpolationSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (PinHiddenByDefault))
	FFootPlacementTraceSettings TraceSettings;

	// TODO: This wont work well when the animation doesn't have a single plant plane, i.e. a walking upstairs anim
	UPROPERTY(EditAnywhere, Category = "Plant")
	FBoneReference IKFootRootBone;

	UPROPERTY(EditAnywhere, Category = "Pelvis")
	FBoneReference PelvisBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pelvis", meta = (PinHiddenByDefault))
	FFootPlacementPelvisSettings PelvisSettings;

public:
	FAnimNode_FootPlacement();

	// FAnimNode_Base interface
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(
		FComponentSpacePoseContext& Output, 
		TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase

private:
	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	void GatherPelvisDataFromInputs(const UE::Anim::FootPlacement::FEvaluationContext& Context);
	void GatherLegDataFromInputs(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		UE::Anim::FootPlacement::FLegRuntimeData& LegData,
		const FFootPlacemenLegDefinition& LegDef);
	void ProcessCharacterState(const UE::Anim::FootPlacement::FEvaluationContext& Context);
	void ProcessFootAlignment(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		UE::Anim::FootPlacement::FLegRuntimeData& LegData);

	// Evaluates the Pelvis offsets required by the different planting bones and produces a result that best
	// acommodates all of them
	FTransform SolvePelvis(const UE::Anim::FootPlacement::FEvaluationContext& Context);

	FTransform UpdatePelvisInterpolation(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		const FTransform& TargetPelvisTransform);

	// Post-processing adjustments + fix hyper-extension/compression
	UE::Anim::FootPlacement::FPlantResult FinalizeFootAlignment(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		UE::Anim::FootPlacement::FLegRuntimeData& LegData,
		const FFootPlacemenLegDefinition& LegDef,
		const FTransform& PelvisTransformCS);

	FVector GetApproachDirWS(const FAnimationBaseContext& Context) const;

private:
	float CachedDeltaTime = 0.0f;
	FVector LastComponentLocation = FVector::ZeroVector;

	TArray<UE::Anim::FootPlacement::FLegRuntimeData> LegsData;
	UE::Anim::FootPlacement::FPlantRuntimeSettings PlantRuntimeSettings;
	UE::Anim::FootPlacement::FPelvisRuntimeData PelvisData;
	UE::Anim::FootPlacement::FCharacterData CharacterData;

	// Whether we want to plant, independently from any dynamic pose adjustments we may do
	bool WantsToPlant(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose) const;

	// Get Alignment Alpha based on current foot speed
	// 0.0 is fully unaligned and the foot is in flight.
	// 1.0 is fully aligned and the foot is planted.
	float GetAlignmentAlpha(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose) const;

	// This function looks at both the foot bone and the ball bone, returning the smallest distance to the
	// planting plane. Note this distance can be negative, meaning it's penetrating.
	float CalcTargetPlantPlaneDistance(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose) const;

	struct FPelvisOffsetRangeForLimb
	{
		float MaxExtension;
		float MinExtension;
		float DesiredExtension;
	};

	// Find the horizontal pelvis offset range for the foot to reach:
	void FindPelvisOffsetRangeForLimb(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPoseData,
		const FVector& PlantTargetLocationCS,
		const FTransform& PelvisTransformCS,
		const float LimbLength,
		FPelvisOffsetRangeForLimb& OutPelvisOffsetRangeCS) const;

	// Adjust LastPlantTransformWS to current, to have the foot pivot around the ball instead of the ankle
	FTransform GetFootPivotAroundBallWS(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose,
		const FTransform& LastPlantTransformWS) const;

	// Align the transform the provided world space ground plant plane.
	// Also outputs the twist along the ground plane needed to get there
	void AlignPlantToGround(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		const FPlane& PlantPlaneWS,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose,
		FTransform& InOutFootTransformWS,
		FQuat& OutTwistCorrection) const;

	// Handles horizontal interpolation when unlocking the plant
	FTransform UpdatePlantOffsetInterpolation(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		UE::Anim::FootPlacement::FLegRuntimeData::FInterpolationData& InOutInterpData,
		const FTransform& DesiredTransformCS) const;

	// Handles the interpolation of the planting plane. Because the plant transform is specified with respect to the 
	// planting plane, it cannot change abruptly without causing an animation pop. It must be interpolated instead.
	void UpdatePlantingPlaneInterpolation(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		const FTransform& FootTransformWS,
		const FTransform& LastAlignedFootTransform,
		const float AlignmentAlpha,
		FPlane& InOutPlantPlane,
		UE::Anim::FootPlacement::FLegRuntimeData::FInterpolationData& InOutInterpData) const;

	// Checks unplanting and replanting conditions to determine if the foot is planted
	void DeterminePlantType(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		const FTransform& FKTransformWS,
		const FTransform& CurrentBoneTransformWS,
		UE::Anim::FootPlacement::FLegRuntimeData::FPlantData& InOutPlantData,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose) const;
	
	float GetMaxLimbExtension(const float DesiredExtension, const float LimbLength) const;
	float GetMinLimbExtension(const float DesiredExtension, const float LimbLength) const;

#if ENABLE_ANIM_DEBUG
	UE::Anim::FootPlacement::FDebugData DebugData;
	void DrawDebug(
		const UE::Anim::FootPlacement::FEvaluationContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData& LegData,
		const UE::Anim::FootPlacement::FPlantResult& PlantResult) const;
#endif

	bool bIsFirstUpdate = false;
	FGraphTraversalCounter UpdateCounter;
};

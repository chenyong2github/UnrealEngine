// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoneControllers/BoneControllerTypes.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNode_OrientationWarping.generated.h"

USTRUCT()
struct FOrientationWarpingSpineBoneSettings
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference Bone;
};

USTRUCT()
struct FOrientationWarpingSpineBoneData
{
	GENERATED_USTRUCT_BODY()

	FCompactPoseBoneIndex BoneIndex;
	float Weight;

	FOrientationWarpingSpineBoneData()
		: BoneIndex(INDEX_NONE)
		, Weight(0.f)
	{}

	FOrientationWarpingSpineBoneData(FCompactPoseBoneIndex InBoneIndex)
		: BoneIndex(InBoneIndex)
		, Weight(0.f)
	{}

	// Comparison Operator for Sorting.
	struct FCompareBoneIndex
	{
		FORCEINLINE bool operator()(const FOrientationWarpingSpineBoneData& A, const FOrientationWarpingSpineBoneData& B) const
		{
			return A.BoneIndex < B.BoneIndex;
		}
	};
};

USTRUCT(BlueprintType)
struct FOrientationWarpingSettings 
{
	GENERATED_USTRUCT_BODY()

	/** Rotation Axis used to rotate mesh. */
	UPROPERTY(EditAnywhere, Category = Settings)
	TEnumAsByte<EAxis::Type> YawRotationAxis = EAxis::Z;

	/** How much of rotation is on whole body versus on IK Feet. */
	UPROPERTY(EditAnywhere, Category = "Settings")
	float BodyOrientationAlpha = 0.5f;

	/** Spine bones countering the rotation of the body, so character keeps aiming straight ahead. */
	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FOrientationWarpingSpineBoneSettings> SpineBones;

	/** IK Foot Root bone. To be rotated if 'BodyOrientationAlpha' is less than 1.f */
	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference IKFootRootBone;

	/** IK Foot Bones. To be rotated if 'BodyOrientationAlpha' is less than 1.f */
	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FBoneReference> IKFootBones;
};

USTRUCT(BlueprintInternalUseOnly)
struct ANIMATIONWARPINGRUNTIME_API FAnimNode_OrientationWarping : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Evaluation, meta = (PinHiddenByDefault))
	EWarpingEvaluationMode Mode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinShownByDefault))
	float LocomotionAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	FOrientationWarpingSettings Settings;

	UPROPERTY(Transient)
	TArray<FOrientationWarpingSpineBoneData> SpineBoneDataArray;

	TArray<FCompactPoseBoneIndex> IKFootBoneIndexArray;

	FCompactPoseBoneIndex IKFootRootBoneIndex;

public:
	FAnimNode_OrientationWarping();

	// FAnimNode_Base interface
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface


private:
	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface
};

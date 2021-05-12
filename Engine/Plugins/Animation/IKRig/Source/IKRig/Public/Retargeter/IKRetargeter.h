// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/SkeletalMesh.h"
#include "Containers/UnrealString.h"
#include "Math/TransformNonVectorized.h"

#include "IKRigDefinition.h"
#include "IKRigProcessor.h"

#include "IKRetargeter.generated.h"

USTRUCT(Blueprintable)
struct IKRIG_API FIKRetargetChainSettings
{
	GENERATED_BODY()

	/** Range -1 to 1. Default 0. Rotates chain away or towards the midline (positive values will raise arms upwards). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Offsets, meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0"))
	float AbductionOffset = 0.0f;

	/** Range -1 to 1. Default 0. Rotates chain forward (+) and backward (-) relative to facing direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Offsets, meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0"))
	float SwingOffset = 0.0f;

	/** Range -1 to 1. Default 0. Rotates the chain along an axis from the start to the end of the chain.
	*  At -1 the chain is rotated laterally 90 degrees. At +1 the chain is rotated medially 90 degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Offsets, meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0"))
	float TwistOffset = 0.0f;

	/** Range -1 to 2. Default 0. Brings IK effector closer (-) or further (+) from origin of chain.
	*  At -1 the end is on top of the origin. At +2 the end is fully extended twice the length of the chain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Offsets, meta = (ClampMin = "-1.0", ClampMax = "2.0", UIMin = "-1.0", UIMax = "2.0"))
	float Extension = 0.0f;

	/** Range 0 to 1. Default 0. Allow the chain to stretch by translating to reach the IK goal locations.
	*  At 0 the chain will not stretch at all. At 1 the chain will be allowed to stretch double it's full length to reach IK. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stretch, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float StretchTolerance = 0.0f;

	/** Range 0 to 1. Default is 1.0. Blend IK effector at the end of this chain towards the original position
	 *  on the source skeleton (0.0) or the position on the retargeted target skeleton (1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float IkToSourceOrTarget = 1.0f;
	
	/** When true, the source IK position is calculated relative to a source bone and applied relative to a target bone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode)
	bool bIkRelativeToSourceBone = false;
	
	/** A bone in the SOURCE skeleton that the IK location is relative to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode, meta = (EditCondition="bIkRelativeToSourceBone"))
	FName IkRelativeSourceBone;
	
	/** A bone in the TARGET skeleton that the IK location is relative to.
	 * This is usually the same bone as the source skeleton, but may have a different name in the target skeleton. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode, meta = (EditCondition="bIkRelativeToSourceBone"))
	FName IkRelativeTargetBone;	
};

struct FRetargetSkeleton
{
	TArray<FName> BoneNames;
	TArray<int32> ParentIndices;
	TArray<FTransform> RetargetGlobalPose;
	FString SkeletalMeshName;

	void InitializeHierarchy(const USkeletalMesh* SkeletalMesh, const TArray<FTransform>& InLocalRetargetPose);
	int32 FindBoneIndexByName(const FName InName) const;
	int32 GetParentIndex(const int32 BoneIndex) const;
};

struct FTargetSkeleton : public FRetargetSkeleton
{
	TArray<FTransform> OutputGlobalPose;
	TArray<FTransform> RetargetLocalPose;
	FString SkeletalMeshName;
	TArray<bool> IsBoneFK;

	void Initialize(const USkeletalMesh* SkeletalMesh, const TArray<FTransform>& InTargetRetargetLocalPose);
	void SetBoneControlledByFKChain(const int32 BoneIndex, const bool IsFK);
	void UpdateGlobalTransformsBelowBone(const int32 StartBoneIndex);
	void UpdateGlobalTransformsAllNonFKBones();

private:
	void UpdateGlobalTransformOfSingleBone(const int32 BoneIndex);
};

struct FEncodedRoot
{
	FVector NormalizedPosition;
	
	FQuat Rotation;

	FQuat InitialRotation;
};

struct FRootEncoder
{
	int32 BoneIndex;

	float InvInitialHeight;

	FEncodedRoot EncodedResult;

	bool Initialize(const FName RootBoneName, const FRetargetSkeleton& SourceSkeleton);

	void EncodePose(const TArray<FTransform> &InputGlobalPose);
};

struct FRootDecoder
{
	int32 BoneIndex;
	
	float InitialHeight;

	FQuat InitialRotation;

	bool Initialize(const FName RootBoneName, const FTargetSkeleton& TargetSkeleton);

	void DecodePose(
        const FEncodedRoot& EncodedResult,
        TArray<FTransform> &InOutTargetBoneTransforms,
        const float StrideScale) const;
};

struct FChainEncoderFK
{
	TArray<FTransform> InitialBoneTransforms;

	TArray<FTransform> CurrentBoneTransforms;

	TArray<float> Params;

	bool Initialize(
		const TArray<int32>& BoneIndices,
		const TArray<FTransform> &InitialGlobalPose);
	
	bool CalculateBoneParameters();

	void EncodePose(
		const TArray<int32>& SourceBoneIndices,
		const TArray<FTransform> &InputGlobalPose);

	void DecodePose(
		const TArray<int32>& TargetBoneIndices,
		const FChainEncoderFK& SourceChain,
		TArray<FTransform> &InOutGlobalPose);

	FTransform GetTransformAtParam(
		const TArray<FTransform>& Transforms,
		const TArray<float>& InParams,
		const float Param) const;
};

struct FEncodedIKChain
{
	FVector EndDirectionNormalized;
	FQuat EndRotation;
	FQuat EndRotationOrig;
	float HeightFromGroundNormalized;
	FVector PoleVectorDirection;
};

struct FChainEncoderIK
{
	int32 BoneIndexA;
	
	int32 BoneIndexB;
	
	int32 BoneIndexC;

	FVector EndPositionOrig;
	
	FQuat EndRotationOrig;
	
	float InverseLength;

	FEncodedIKChain EncodedResult;

	bool Initialize(const TArray<int32>& BoneIndices, const TArray<FTransform> &InitialGlobalPose);

	void EncodePose(const TArray<FTransform> &InputGlobalPose);
};

struct FDecodedIKChain
{
	FVector EndEffectorPosition;
	
	FQuat EndEffectorRotation;
	
	FVector PoleVectorPosition;
};

struct FChainDecoderIK
{
	int32 BoneIndexA;

	float Length;

	FVector EndPositionOrig;
	
	FQuat EndRotationOrig;

	FDecodedIKChain DecodedResults;

	bool Initialize(const TArray<int32>& BoneIndices, const TArray<FTransform> &InitialGlobalPose);
	
	void DecodePose(
		const FEncodedIKChain& EncodedChain,
		const TArray<FTransform>& OutGlobalPose,
		FDecodedIKChain& OutResults) const;
};

struct FRetargetChainPair
{
	TArray<int32> SourceBoneIndices;
	
	TArray<int32> TargetBoneIndices;
	
	FName SourceBoneChainName;
	
	FName TargetBoneChainName;

	virtual ~FRetargetChainPair(){};
	
	virtual bool Initialize(
		const FBoneChain& SourceBoneChain,
		const FBoneChain& TargetBoneChain,
		const FRetargetSkeleton& SourceSkeleton,
		const FTargetSkeleton& TargetSkeleton);

private:

	bool ValidateBoneChainWithSkeletalMesh(
		const bool IsSource,
		const FBoneChain& BoneChain,
		const FRetargetSkeleton& RetargetSkeleton);
};

struct FRetargetChainPairFK : FRetargetChainPair
{
	FChainEncoderFK FKEncoder;

	FChainEncoderFK FKDecoder;
	
	virtual bool Initialize(
        const FBoneChain& SourceBoneChain,
        const FBoneChain& TargetBoneChain,
        const FRetargetSkeleton& SourceSkeleton,
        const FTargetSkeleton& TargetSkeleton) override;
};

struct FRetargetChainPairIK : FRetargetChainPair
{
	FChainEncoderIK IKEncoder;
	
	FChainDecoderIK IKDecoder;
	
	FName IKGoalName;
	
	FName PoleVectorGoalName;

	virtual bool Initialize(
        const FBoneChain& SourceBoneChain,
        const FBoneChain& TargetBoneChain,
        const FRetargetSkeleton& SourceSkeleton,
        const FTargetSkeleton& TargetSkeleton) override;
};

UCLASS(Blueprintable)
class IKRIG_API UIKRetargeter : public UObject
{
	GENERATED_BODY()
	
public:

	UPROPERTY(EditAnywhere, Category = Rigs)
	UIKRigDefinition *SourceIKRigAsset;

	UPROPERTY(EditAnywhere, Category = Rigs)
	UIKRigDefinition *TargetIKRigAsset;

	UPROPERTY(EditAnywhere, Category = Mapping)
	TMap<FName, FName> ChainMapping;

	FRetargetSkeleton SourceSkeleton;
	
	FTargetSkeleton TargetSkeleton;
	
	bool bIsLoadedAndValid;

	UIKRetargeter();
	
	void Initialize(USkeletalMesh *SourceSkeleton, USkeletalMesh *TargetSkeleton, UObject* Outer);
	
	void RunRetargeter(const TArray<FTransform>& InSourceGlobalPose, bool bEnableIK);

private:

	UPROPERTY(Transient)
	UIKRigProcessor* IKRigProcessor = nullptr;
	
	TArray<FRetargetChainPairFK> ChainPairsFK;
	
	TArray<FRetargetChainPairIK> ChainPairsIK;

	FRootEncoder RootEncoder;
	
	FRootDecoder RootDecoder;

	bool InitializeRoots();
		
	bool InitializeBoneChainPairs();
	
	bool InitializeIKRig(UObject* Outer, const FReferenceSkeleton& InRefSkeleton);

	void RunRootRetarget(
		const TArray<FTransform>& InGlobalTransforms,
		TArray<FTransform>& OutGlobalTransforms);
	
	void RunFKRetarget(
		const TArray<FTransform>& InGlobalTransforms,
		TArray<FTransform>& OutGlobalTransforms,
		const bool bPreIK);

	void RunIKRetarget(
		const TArray<FTransform>& InGlobalPose,
		TArray<FTransform>& OutGlobalPose);
};

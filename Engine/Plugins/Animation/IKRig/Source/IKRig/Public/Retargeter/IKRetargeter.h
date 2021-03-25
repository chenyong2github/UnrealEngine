// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/SkeletalMesh.h"
#include "Containers/UnrealString.h"
#include "Math/TransformNonVectorized.h"

#include "IKRigDefinition.h"
#include "IKRigProcessor.h"

#include "IKRetargeter.generated.h"

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
	UIKRigProcessor* IKRig = nullptr;
	
	TArray<FRetargetChainPairFK> ChainPairsFK;
	
	TArray<FRetargetChainPairIK> ChainPairsIK;

	FRootEncoder RootEncoder;
	
	FRootDecoder RootDecoder;

	bool InitializeRoots();
		
	bool InitializeBoneChainPairs();
	
	bool InitializeIKRig(UObject* Outer);

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

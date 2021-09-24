// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

#include "IKRigDefinition.h"
#include "IKRigProcessor.h"

#include "IKRetargeter.generated.h"

struct FIKRetargetPose;

USTRUCT(Blueprintable)
struct IKRIG_API FRetargetChainMap
{
	GENERATED_BODY()

	FRetargetChainMap(){}
	
	FRetargetChainMap(const FName& TargetChain) : TargetChain(TargetChain){}
	
	/** The name of the chain on the Source IK Rig asset to copy animation from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Offsets)
	FName SourceChain = NAME_None;

	/** The name of the chain on the Target IK Rig asset to copy animation from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Offsets)
	FName TargetChain = NAME_None;

	/** Range -1 to 2. Default 0. Brings IK effector closer (-) or further (+) from origin of chain.
	*  At -1 the end is on top of the origin. At +2 the end is fully extended twice the length of the chain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Offsets, meta = (ClampMin = "-1.0", ClampMax = "2.0", UIMin = "-1.0", UIMax = "2.0"))
	float Extension = 0.0f;

	/** Range 0 to 1. Default 0. Allow the chain to stretch by translating to reach the IK goal locations.
	*  At 0 the chain will not stretch at all. At 1 the chain will be allowed to stretch double it's full length to reach IK. */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stretch, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	//float StretchTolerance = 0.0f;

	/** Range 0 to 1. Default is 1.0. Blend IK effector at the end of this chain towards the original position
	 *  on the source skeleton (0.0) or the position on the retargeted target skeleton (1.0). */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	//float IkToSourceOrTarget = 1.0f;
	
	/** When true, the source IK position is calculated relative to a source bone and applied relative to a target bone. */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode)
	//bool bIkRelativeToSourceBone = false;
	
	/** A bone in the SOURCE skeleton that the IK location is relative to. */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode, meta = (EditCondition="bIkRelativeToSourceBone"))
	//FName IkRelativeSourceBone;
	
	/** A bone in the TARGET skeleton that the IK location is relative to.
	 * This is usually the same bone as the source skeleton, but may have a different name in the target skeleton. */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode, meta = (EditCondition="bIkRelativeToSourceBone"))
	//FName IkRelativeTargetBone;	
};

struct IKRIG_API FRetargetSkeleton
{
	TArray<FName> BoneNames;
	
	TArray<int32> ParentIndices;
	
	TArray<FTransform> RetargetLocalPose;
	
	TArray<FTransform> RetargetGlobalPose;
	
	FString SkeletalMeshName;

	void Initialize(
		USkeletalMesh* SkeletalMesh, 
		FIKRetargetPose* RetargetPose);

	int32 FindBoneIndexByName(const FName InName) const;

	int32 GetParentIndex(const int32 BoneIndex) const;

	void UpdateGlobalTransformsBelowBone(
		const int32 StartBoneIndex,
		TArray<FTransform>& InLocalPose,
		TArray<FTransform>& OutGlobalPose) const;

	void UpdateLocalTransformsBelowBone(
		const int32 StartBoneIndex,
		TArray<FTransform>& OutLocalPose,
		TArray<FTransform>& InGlobalPose) const;
	
	void UpdateGlobalTransformOfSingleBone(
		const int32 BoneIndex,
		const TArray<FTransform>& InLocalPose,
		TArray<FTransform>& OutGlobalPose) const;
	
	void UpdateLocalTransformOfSingleBone(
		const int32 BoneIndex,
		TArray<FTransform>& OutLocalPose,
		TArray<FTransform>& InGlobalPose) const;

	void GetChildrenIndices(const int32 BoneIndex, TArray<int32>& OutChildren) const;
};

struct FTargetSkeleton : public FRetargetSkeleton
{
	TArray<FTransform> OutputGlobalPose;
	TArray<bool> IsBoneRetargeted;

	void Initialize(
		USkeletalMesh* SkeletalMesh, 
		FIKRetargetPose* RetargetPose);

	void SetBoneIsRetargeted(const int32 BoneIndex, const bool IsRetargeted);

	void UpdateGlobalTransformsAllNonRetargetedBones(TArray<FTransform>& InOutGlobalPose);
};

struct FRootSource
{
	int32 BoneIndex;

	FQuat InitialRotation;

	float InitialHeightInverse;

	FVector CurrentPositionNormalized;
	
	FQuat CurrentRotation;
};

struct FRootTarget
{
	int32 BoneIndex;
	
	FQuat InitialRotation;
	
	float InitialHeight;
};

struct FRootRetargeter
{
	FRootSource Source;
	
	FRootTarget Target;

	bool InitializeSource(const FName SourceRootBoneName, const FRetargetSkeleton& SourceSkeleton);
	
	bool InitializeTarget(const FName TargetRootBoneName, const FTargetSkeleton& TargetSkeleton);

	void EncodePose(const TArray<FTransform> &SourceGlobalPose);
	
	void DecodePose( TArray<FTransform> &OutTargetGlobalPose, const float StrideScale) const;
};

struct FChainFK
{
	TArray<FTransform> InitialBoneTransforms;

	TArray<FTransform> CurrentBoneTransforms;

	TArray<float> Params;

	bool Initialize(
		const TArray<int32>& BoneIndices,
		const TArray<FTransform> &InitialGlobalPose);

private:
	
	bool CalculateBoneParameters();	
};

struct FChainEncoderFK : public FChainFK
{
	void EncodePose(
		const TArray<int32>& SourceBoneIndices,
		const TArray<FTransform> &InputGlobalPose);
};

struct FChainDecoderFK : public FChainFK
{
	void InitializeIntermediateParentIndices(
		const int32 RetargetRootBoneIndex,
		const int32 ChainRootBoneIndex,
		const FTargetSkeleton& TargetSkeleton);
	
	void DecodePose(
		const TArray<int32>& TargetBoneIndices,
		const FChainEncoderFK& SourceChain,
		const FTargetSkeleton& TargetSkeleton,
		TArray<FTransform> &InOutGlobalPose);

private:

	FTransform GetTransformAtParam(
		const TArray<FTransform>& Transforms,
		const TArray<float>& InParams,
		const float Param) const;
	
	void UpdateIntermediateParents(
		const FTargetSkeleton& TargetSkeleton,
		TArray<FTransform> &InOutGlobalPose);

	TArray<int32> IntermediateParentIndices;
};

struct FDecodedIKChain
{
	FVector EndEffectorPosition;
	
	FQuat EndEffectorRotation;
	
	FVector PoleVectorPosition;
};

struct FSourceChainIK
{
	int32 BoneIndexA;
	
	int32 BoneIndexB;
	
	int32 BoneIndexC;
	
	FVector InitialEndPosition;
	
	FQuat InitialEndRotation;
	
	float InvInitialLength;

	// results after encoding...
	
	FVector CurrentEndDirectionNormalized;
	
	FQuat CurrentEndRotation;
	
	float CurrentHeightFromGroundNormalized;
	
	FVector PoleVectorDirection;
};

struct FTargetChainIK
{
	int32 BoneIndexA;
	
	float InitialLength;
	
	FVector InitialEndPosition;
	
	FQuat InitialEndRotation;
};

struct FChainRetargeterIK
{
	FSourceChainIK Source;
	
	FTargetChainIK Target;

	bool InitializeSource(const TArray<int32>& BoneIndices, const TArray<FTransform> &SourceInitialGlobalPose);
	
	bool InitializeTarget(const TArray<int32>& BoneIndices, const TArray<FTransform> &TargetInitialGlobalPose);

	void EncodePose(const TArray<FTransform> &SourceInputGlobalPose);
	
	void DecodePose(const TArray<FTransform>& OutGlobalPose, FDecodedIKChain& OutResults) const;
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

	FChainDecoderFK FKDecoder;
	
	virtual bool Initialize(
        const FBoneChain& SourceBoneChain,
        const FBoneChain& TargetBoneChain,
        const FRetargetSkeleton& SourceSkeleton,
        const FTargetSkeleton& TargetSkeleton) override;
};

struct FRetargetChainPairIK : FRetargetChainPair
{	
	FChainRetargeterIK IKChainRetargeter;
	
	FName IKGoalName;
	
	FName PoleVectorGoalName;

	virtual bool Initialize(
        const FBoneChain& SourceBoneChain,
        const FBoneChain& TargetBoneChain,
        const FRetargetSkeleton& SourceSkeleton,
        const FTargetSkeleton& TargetSkeleton) override;
};

USTRUCT()
struct IKRIG_API FIKRetargetPose
{
	GENERATED_BODY()
	
	FIKRetargetPose(){}
	
	UPROPERTY(EditAnywhere, Category = RetargetPose)
	FVector RootTranslationOffset = FVector::ZeroVector;
	
	UPROPERTY(EditAnywhere, Category = RetargetPose)
	FQuat RootRotationOffset = FQuat::Identity;
	
	UPROPERTY(EditAnywhere, Category = RetargetPose)
	TMap<FName, FQuat> BoneRotationOffsets;

	void AddRotationDeltaToBone(FName BoneName, FQuat RotationDelta);
};

UCLASS(Blueprintable)
class IKRIG_API UIKRetargeter : public UObject
{
	GENERATED_BODY()
	
public:

	/** The rig to copy animation FROM.*/
	UPROPERTY(VisibleAnywhere, Category = Rigs)
	UIKRigDefinition *SourceIKRigAsset;
	
	/** The rig to copy animation TO.*/
	UPROPERTY(EditAnywhere, Category = Rigs)
	UIKRigDefinition *TargetIKRigAsset;

	/** Mapping of chains to copy animation between source and target rigs.*/
	UPROPERTY()
	TArray<FRetargetChainMap> ChainMapping;

	/** The set of retarget poses available as options for retargeting.*/
	UPROPERTY()
	FName CurrentRetargetPose = DefaultPoseName;
	static const FName DefaultPoseName;
	
	/** The set of retarget poses available as options for retargeting.*/
	UPROPERTY(VisibleAnywhere, Category = RetargetPose)
	TMap<FName, FIKRetargetPose> RetargetPoses;

	/** When false, translational motion of skeleton root is not copied. Useful for debugging.*/
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bRetargetRoot = true;
	
	/** When false, limbs are not copied via FK. Useful for debugging limb issues suspected to be caused by FK pose.*/
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bRetargetFK = true;
	
	/** When false, IK is not applied as part of retargeter. Useful for debugging limb issues suspected to be caused by IK.*/
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bRetargetIK = true;

	/** Move the target actor in the viewport for easier visualization next to the source actor.*/
	UPROPERTY(EditAnywhere, Category = DebugPreview, meta = (UIMin = "-2000.0", UIMax = "2000.0"))
	float TargetActorOffset = 150.0f;

	/** Scale the target actor in the viewport for easier visualization next to the source actor.*/
	UPROPERTY(EditAnywhere, Category = DebugPreview, meta = (UIMin = "0.01", UIMax = "10.0"))
	float TargetActorScale = 1.0f;

	/** The visual size of the bones in the viewport when editing the retarget pose.*/
	UPROPERTY(EditAnywhere, Category = DebugPreview, meta = (ClampMin = "0.0", UIMin = "0.01", UIMax = "10.0"))
	float BoneDrawSize = 8.0f;

	/** The visual thickness of the bones in the viewport when editing the retarget pose.*/
	UPROPERTY(EditAnywhere, Category = DebugPreview, meta = (ClampMin = "0.0", UIMin = "0.01", UIMax = "10.0"))
	float BoneDrawThickness = 1.0f;

	/** The internal data structures used to represent the SOURCE skeleton / pose during retargeter.*/
	FRetargetSkeleton SourceSkeleton;

	/** The internal data structures used to represent the TARGET skeleton / pose during retargeter.*/
	FTargetSkeleton TargetSkeleton;

	/** Only true once Initialize() has successfully completed.*/
	bool bIsLoadedAndValid = false;

	/** A special editor-only mode which forces the retargeter to output the current retarget reference pose,
	 * rather than actually running the retarget and outputting the retargeted pose. Used in Edit-Pose mode.*/
	UPROPERTY()
	bool bEditReferencePoseMode = false;
	/** Used in-editor to force reinitialization when the asset version of the source asset differs from the currently
	 *running asset version.*/
	UPROPERTY()
	int32 AssetVersion = 0;

	UIKRetargeter();

	/**
	* Initialize the retargeter to enable running it.
	* @param SourceSkeleton - the skeletal mesh to poses FROM
	* @param TargetSkeleton - the skeletal mesh to poses TO
	* @param Outer - a UObject that will live for the duration of the retargeting workload
	* @warning - Initialization does a lot of validation and can fail for many reasons. Check bIsLoadedAndValid afterwards.
	*/
	void Initialize(USkeletalMesh *SourceSkeleton, USkeletalMesh *TargetSkeleton, UObject* Outer);

	/**
	 * Run the retarget to generate a new pose.
	 * @param InSourceGlobalPose -  is the source mesh input pose in Component/Global space
	 * @return The retargeted Component/Global space pose for the target skeleton
	 */
	TArray<FTransform>& RunRetargeter(const TArray<FTransform>& InSourceGlobalPose);

	/**
	* Get the Global transform, in the currently used retarget pose, for a bone in the target skeletal mesh
	* @param TargetBoneIndex - The index of a bone in the target skeleton.
	* @return The Global space transform for the bone or Identity if bone not found.
	* @warning This function is only valid to call after the retargeter has been initialized (which generates global retarget pose)
	*/
	FTransform GetTargetBoneRetargetPoseGlobalTransform(const int32& TargetBoneIndex) const;

	/**
	* Get the mapping (between source/target) for a given target chain
	* @param TargetChainName - The name of chain on the target IK Rig
	* @return The mapping between the target chain and the source
	*/
	FRetargetChainMap* GetChainMap(const FName& TargetChainName);

	void CleanPoseList();
	
#if WITH_EDITOR
	
	// UObject
	virtual bool Modify(bool bAlwaysMarkDirty = true) override;
	virtual void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;
	// END UObject

	FName GetSourceRootBone();
	FName GetTargetRootBone();

	void GetTargetChainNames(TArray<FName>& OutNames) const;
	void GetSourceChainNames(TArray<FName>& OutNames) const;

	void CleanChainMapping();
	void AutoMapChains();

	USkeleton* GetSourceSkeletonAsset() const;

	void AddRetargetPose(FName NewPoseName);
	void RemoveRetargetPose(FName PoseToRemove);
	void ResetRetargetPose(FName PoseToReset);
	void SetCurrentRetargetPose(FName CurrentPose);
	void AddRotationOffsetToRetargetPoseBone(FName BoneName, FQuat RotationOffset);
	
#endif

private:

	UPROPERTY(Transient)
	TObjectPtr<UIKRigProcessor> IKRigProcessor;
	
	TArray<FRetargetChainPairFK> ChainPairsFK;
	
	TArray<FRetargetChainPairIK> ChainPairsIK;
	
	FRootRetargeter RootRetargeter;

	bool InitializeRoots();
		
	bool InitializeBoneChainPairs();
	
	bool InitializeIKRig(UObject* Outer, const FReferenceSkeleton& InRefSkeleton);
	
	void RunRootRetarget(const TArray<FTransform>& InGlobalTransforms, TArray<FTransform>& OutGlobalTransforms);
	
	void RunFKRetarget(const TArray<FTransform>& InGlobalTransforms, TArray<FTransform>& OutGlobalTransforms);

	void RunIKRetarget(const TArray<FTransform>& InGlobalPose, TArray<FTransform>& OutGlobalPose);
};

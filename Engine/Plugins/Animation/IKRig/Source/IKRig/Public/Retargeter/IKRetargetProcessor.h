// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IKRetargeter.h"
#include "IKRigLogger.h"
#include "Kismet/KismetMathLibrary.h"
#include "IKRetargetProcessor.generated.h"

enum class EIKRetargetSkeletonMode : uint8;
class URetargetChainSettings;
class UIKRigDefinition;
class UIKRigProcessor;
struct FReferenceSkeleton;
struct FBoneChain;
struct FIKRetargetPose;
class UObject;
class UIKRetargeter;
class USkeletalMesh;

struct IKRIG_API FRetargetSkeleton
{
	TArray<FName> BoneNames;
	
	TArray<int32> ParentIndices;
	
	TArray<FTransform> RetargetLocalPose;
	
	TArray<FTransform> RetargetGlobalPose;
	
	USkeletalMesh* SkeletalMesh;

	// record which chain is actually controlling each bone
	TArray<FName> ChainThatContainsBone;

	void Initialize(
		USkeletalMesh* InSkeletalMesh,
		const TArray<FBoneChain>& BoneChains,
		const FIKRetargetPose* RetargetPose,
		const FName& RetargetRootBone);

	void Reset();

	void GenerateRetargetPose(const FIKRetargetPose* InRetargetPose, const FName& RetargetRootBone);

	int32 FindBoneIndexByName(const FName InName) const;

	int32 GetParentIndex(const int32 BoneIndex) const;

	void UpdateGlobalTransformsBelowBone(
		const int32 StartBoneIndex,
		const TArray<FTransform>& InLocalPose,
		TArray<FTransform>& OutGlobalPose) const;

	void UpdateLocalTransformsBelowBone(
		const int32 StartBoneIndex,
		TArray<FTransform>& OutLocalPose,
		const TArray<FTransform>& InGlobalPose) const;
	
	void UpdateGlobalTransformOfSingleBone(
		const int32 BoneIndex,
		const TArray<FTransform>& InLocalPose,
		TArray<FTransform>& OutGlobalPose) const;
	
	void UpdateLocalTransformOfSingleBone(
		const int32 BoneIndex,
		TArray<FTransform>& OutLocalPose,
		const TArray<FTransform>& InGlobalPose) const;

	FTransform GetGlobalRefPoseOfSingleBone(
		const int32 BoneIndex,
		const TArray<FTransform>& InGlobalPose) const;

	int32 GetCachedEndOfBranchIndex(const int32 InBoneIndex) const;

	void GetChildrenIndices(const int32 BoneIndex, TArray<int32>& OutChildren) const;

	void GetChildrenIndicesRecursive(const int32 BoneIndex, TArray<int32>& OutChildren) const;
	
	bool IsParentOfChild(const int32 PotentialParentIndex, const int32 ChildBoneIndex) const;

	FQuat GetRetargetPoseDeltaRotation(const FName BoneName, const FIKRetargetPose* InRetargetPose) const;

private:
	
	/** One index per-bone. Lazy-filled on request. Stores the last element of the branch below the bone.
	 * You can iterate between in the indices stored here and the bone in question to iterate over all children recursively */
	mutable TArray<int32> CachedEndOfBranchIndices;
};

struct FTargetSkeleton : public FRetargetSkeleton
{
	TArray<FTransform> OutputGlobalPose;
	// true for bones that are in a target chain that is ALSO mapped to a source chain
	// ie, bones that are actually posed based on a mapped source chain
	TArray<bool> IsBoneRetargeted;

	void Initialize(
		USkeletalMesh* InSkeletalMesh,
		const TArray<FBoneChain>& BoneChains,
		const FIKRetargetPose* RetargetPose,
		const FName& RetargetRootBone);

	void Reset();

	void SetBoneIsRetargeted(const int32 BoneIndex, const bool IsRetargeted);

	void UpdateGlobalTransformsAllNonRetargetedBones(TArray<FTransform>& InOutGlobalPose);
};

/** resolving an FBoneChain to an actual skeleton, used to validate compatibility and get all chain indices */
struct IKRIG_API FResolvedBoneChain
{
	FResolvedBoneChain(const FBoneChain& BoneChain, const FRetargetSkeleton& Skeleton, TArray<int32> &OutBoneIndices);

	/* Does the START bone exist in the skeleton? */
	bool bFoundStartBone = false;
	/* Does the END bone exist in the skeleton? */
	bool bFoundEndBone = false;
	/* Is the END bone equals or a child of the START bone? */
	bool bEndIsStartOrChildOfStart  = false;

	bool IsValid() const
	{
		return bFoundStartBone && bFoundEndBone && bEndIsStartOrChildOfStart;
	}
};

struct FRootSource
{
	int32 BoneIndex;
	FQuat InitialRotation;
	float InitialHeightInverse;
	FVector InitialPosition;
	FVector CurrentPosition;
	FVector CurrentPositionNormalized;
	FQuat CurrentRotation;
};

struct FRootTarget
{
	int32 BoneIndex;

	FVector InitialPosition;
	FQuat InitialRotation;
	
	float InitialHeight;
};

struct FRootRetargeter
{	
	FRootSource Source;
	
	FRootTarget Target;

	float GlobalScaleHorizontal = 1.0f;

	float GlobalScaleVertical = 1.0f;

	FVector BlendToSource = FVector::ZeroVector;

	FVector StaticOffset = FVector::ZeroVector;

	FRotator StaticRotationOffset = FRotator::ZeroRotator;

	void Reset();
	
	bool InitializeSource(
		const FName SourceRootBoneName,
		const FRetargetSkeleton& SourceSkeleton,
		FIKRigLogger& Log);
	
	bool InitializeTarget(
		const FName TargetRootBoneName,
		const FTargetSkeleton& TargetSkeleton,
		FIKRigLogger& Log);

	void EncodePose(const TArray<FTransform> &SourceGlobalPose);
	
	void DecodePose( TArray<FTransform> &OutTargetGlobalPose) const;

	FVector GetGlobalScaleVector() const
	{
		return FVector(GlobalScaleHorizontal, GlobalScaleHorizontal, GlobalScaleVertical);
	}
};

struct FRetargetChainSettings
{
	FName TargetChainName;

	bool CopyPoseUsingFK = true;
	ERetargetRotationMode RotationMode;
	float RotationAlpha = 1.0f;
	ERetargetTranslationMode TranslationMode;
	float TranslationAlpha = 1.0f;

	bool DriveIKGoal = true;
	FVector StaticOffset;
	FVector StaticLocalOffset;
	FRotator StaticRotationOffset;
	float Extension = 1.0f;
	float BlendToSource = 0.0f;
	FVector BlendToSourceWeights = FVector::OneVector;

	bool UseSpeedCurveToPlantIK = false;
	float SpeedThreshold = 0.0f;
	FName SpeedCurveName;
	float UnplantStiffness = 250.0f;
	float UnplantCriticalDamping = 1.0f;

public:
	
	void CopySettingsFromAsset(const URetargetChainSettings* AssetChainSettings)
	{
		TargetChainName = AssetChainSettings->TargetChain;

		CopyPoseUsingFK = AssetChainSettings->CopyPoseUsingFK;
		RotationMode = AssetChainSettings->RotationMode;
		RotationAlpha = AssetChainSettings->RotationAlpha;
		TranslationMode = AssetChainSettings->TranslationMode;
		TranslationAlpha = AssetChainSettings->TranslationAlpha;
		
		DriveIKGoal = AssetChainSettings->DriveIKGoal;
		Extension = AssetChainSettings->Extension;
		StaticOffset = AssetChainSettings->StaticOffset;
		StaticLocalOffset = AssetChainSettings->StaticLocalOffset;
		StaticRotationOffset = AssetChainSettings->StaticRotationOffset;
		BlendToSource = AssetChainSettings->BlendToSource;
		BlendToSourceWeights = AssetChainSettings->BlendToSourceWeights;

		UseSpeedCurveToPlantIK = AssetChainSettings->UseSpeedCurveToPlantIK;
		UnplantStiffness = AssetChainSettings->UnplantStiffness;
		UnplantCriticalDamping = AssetChainSettings->UnplantCriticalDamping;
		SpeedThreshold = AssetChainSettings->VelocityThreshold;
		SpeedCurveName = AssetChainSettings->SpeedCurveName;
	};
};


struct FChainFK
{
	TArray<FTransform> InitialGlobalTransforms;

	TArray<FTransform> InitialLocalTransforms;

	TArray<FTransform> CurrentGlobalTransforms;

	TArray<float> Params;

	int32 ChainParentBoneIndex;
	FTransform ChainParentInitialGlobalTransform;

	bool Initialize(
		const FRetargetSkeleton& Skeleton,
		const TArray<int32>& BoneIndices,
		const TArray<FTransform> &InitialGlobalPose,
		FIKRigLogger& Log);

private:
	
	bool CalculateBoneParameters(FIKRigLogger& Log);

protected:

	static void FillTransformsWithLocalSpaceOfChain(
		const FRetargetSkeleton& Skeleton,
		const TArray<FTransform>& InGlobalPose,
		const TArray<int32>& BoneIndices,
		TArray<FTransform>& OutLocalTransforms);

	void PutCurrentTransformsInRefPose(
		const TArray<int32>& BoneIndices,
		const FRetargetSkeleton& Skeleton,
		const TArray<FTransform>& InCurrentGlobalPose);
};

struct FChainEncoderFK : public FChainFK
{
	TArray<FTransform> CurrentLocalTransforms;

	FTransform ChainParentCurrentGlobalTransform;
	
	void EncodePose(
		const FRetargetSkeleton& SourceSkeleton,
		const TArray<int32>& SourceBoneIndices,
		const TArray<FTransform> &InSourceGlobalPose);

	void TransformCurrentChainTransforms(const FTransform& NewParentTransform);
};

struct FChainDecoderFK : public FChainFK
{
	void InitializeIntermediateParentIndices(
		const int32 RetargetRootBoneIndex,
		const int32 ChainRootBoneIndex,
		const FTargetSkeleton& TargetSkeleton);
	
	void DecodePose(
		const FRootRetargeter& RootRetargeter,
		const FRetargetChainSettings& Settings,
		const TArray<int32>& TargetBoneIndices,
		FChainEncoderFK& SourceChain,
		const FTargetSkeleton& TargetSkeleton,
		TArray<FTransform> &InOutGlobalPose);

private:

	FTransform GetTransformAtParam(
		const TArray<FTransform>& Transforms,
		const TArray<float>& InParams,
		const float& Param) const;
	
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
	FVector PreviousEndPosition;
	FVector CurrentEndPosition;
	
	FVector CurrentEndDirectionNormalized;
	
	FQuat CurrentEndRotation;
	
	float CurrentHeightFromGroundNormalized;
	
	FVector PoleVectorDirection;
};

struct FTargetChainIK
{
	int32 BoneIndexA;

	int32 BoneIndexC;
	
	float InitialLength;
	
	FVector InitialEndPosition;
	
	FQuat InitialEndRotation;

	FVector PrevEndPosition;
};

struct FChainRetargeterIK
{
	FSourceChainIK Source;
	
	FTargetChainIK Target;

	bool ResetThisTick;
	FVectorSpringState PlantingSpringState;

	bool InitializeSource(
		const TArray<int32>& BoneIndices,
		const TArray<FTransform> &SourceInitialGlobalPose,
		FIKRigLogger& Log);
	
	bool InitializeTarget(
		const TArray<int32>& BoneIndices,
		const TArray<FTransform> &TargetInitialGlobalPose,
		FIKRigLogger& Log);

	void EncodePose(const TArray<FTransform> &SourceInputGlobalPose);
	
	void DecodePose(
		const FRetargetChainSettings& Settings,
		const TMap<FName, float>& SpeedValuesFromCurves,
		const float DeltaTime,
		const TArray<FTransform>& OutGlobalPose,
		FDecodedIKChain& OutResults);
};

struct FRetargetChainPair
{
	FRetargetChainSettings Settings;
	
	TArray<int32> SourceBoneIndices;
	
	TArray<int32> TargetBoneIndices;
	
	FName SourceBoneChainName;
	
	FName TargetBoneChainName;

	virtual ~FRetargetChainPair() = default;
	
	virtual bool Initialize(
		URetargetChainSettings* InSettings,
		const FBoneChain& SourceBoneChain,
		const FBoneChain& TargetBoneChain,
		const FRetargetSkeleton& SourceSkeleton,
		const FTargetSkeleton& TargetSkeleton,
		FIKRigLogger& Log);

private:

	bool ValidateBoneChainWithSkeletalMesh(
		const bool IsSource,
		const FBoneChain& BoneChain,
		const FRetargetSkeleton& RetargetSkeleton,
		FIKRigLogger& Log);
};

struct FRetargetChainPairFK : FRetargetChainPair
{
	FChainEncoderFK FKEncoder;

	FChainDecoderFK FKDecoder;
	
	virtual bool Initialize(
		URetargetChainSettings* InSettings,
        const FBoneChain& SourceBoneChain,
        const FBoneChain& TargetBoneChain,
        const FRetargetSkeleton& SourceSkeleton,
        const FTargetSkeleton& TargetSkeleton,
        FIKRigLogger& Log) override;
};

struct FRetargetChainPairIK : FRetargetChainPair
{
	FChainRetargeterIK IKChainRetargeter;
	
	FName IKGoalName;
	
	FName PoleVectorGoalName;

	virtual bool Initialize(
		URetargetChainSettings* Settings,
        const FBoneChain& SourceBoneChain,
        const FBoneChain& TargetBoneChain,
        const FRetargetSkeleton& SourceSkeleton,
        const FTargetSkeleton& TargetSkeleton,
        FIKRigLogger& Log) override;
};

/** The runtime processor that converts an input pose from a source skeleton into an output pose on a target skeleton.
 * To use:
 * 1. Initialize a processor with a Source/Target skeletal mesh and a UIKRetargeter asset.
 * 2. Call RunRetargeter and pass in a source pose as an array of global-space transforms
 * 3. RunRetargeter returns an array of global space transforms for the target skeleton.
 */
UCLASS()
class IKRIG_API UIKRetargetProcessor : public UObject
{
	GENERATED_BODY()

public:

	UIKRetargetProcessor();
	
	/**
	* Initialize the retargeter to enable running it.
	* @param SourceSkeleton - the skeletal mesh to poses FROM
	* @param TargetSkeleton - the skeletal mesh to poses TO
	* @param InRetargeterAsset - the source asset to use for retargeting settings
	* @param bSuppressWarnings - if true, will not output warnings during initialization
	* @warning - Initialization does a lot of validation and can fail for many reasons. Check bIsLoadedAndValid afterwards.
	*/
	void Initialize(
		USkeletalMesh *SourceSkeleton,
		USkeletalMesh *TargetSkeleton,
		UIKRetargeter* InRetargeterAsset,
		const bool bSuppressWarnings=false);

	/**
	* Run the retarget to generate a new pose.
	* @param InSourceGlobalPose -  is the source mesh input pose in Component/Global space
	* @return The retargeted Component/Global space pose for the target skeleton
	*/
	TArray<FTransform>& RunRetargeter(const TArray<FTransform>& InSourceGlobalPose, const TMap<FName, float>& SpeedValuesFromCurves, const float DeltaTime);

	
	FTransform GetTargetBoneRetargetPoseLocalTransform(const int32& TargetBoneIndex) const;

	/** Get read-only access to the target skeleton. */
	const FTargetSkeleton& GetTargetSkeleton() const { return TargetSkeleton; };

	/** Get read-only access to the source skeleton. */
	const FRetargetSkeleton& GetSourceSkeleton() const { return SourceSkeleton; };

	/** Get index of the root bone of the source skeleton. */
	const int32 GetSourceRetargetRoot() const { return RootRetargeter.Source.BoneIndex; };

	/** Get index of the root bone of the target skeleton. */
	const int32 GetTargetRetargetRoot() const { return RootRetargeter.Target.BoneIndex; };
	
	/** Get whether this processor is ready to call RunRetargeter() and generate new poses. */
	bool IsInitialized() const { return bIsInitialized; };

	/** Get whether this processor was initialized with these skeletal meshes and retarget asset*/
	bool WasInitializedWithTheseAssets(
		const TObjectPtr<USkeletalMesh> InSourceMesh,
		const TObjectPtr<USkeletalMesh> InTargetMesh,
		const TObjectPtr<UIKRetargeter> InRetargetAsset);

	/** Get the currently running IK Rig processor for the target */
	UIKRigProcessor* GetTargetIKRigProcessor() const { return IKRigProcessor; };
	
	/** Reset the IK planting state. */
	void ResetPlanting();

	/** logging system */
	FIKRigLogger Log;

#if WITH_EDITOR
	/** Set that this processor needs to be reinitialized. */
	void SetNeedsInitialized();
	/** During editor preview, drive the target IK Rig with the settings from it's source asset */
	void CopyAllSettingsFromAsset();
	/** Returns true if the bone is part of a retarget chain or root bone, false otherwise. */
	bool IsBoneRetargeted(const int32& BoneIndex, const int8& SkeletonToCheck) const;
	/** Returns name of the chain associated with this bone. Returns NAME_None if bone is not in a chain. */
	FName GetChainNameForBone(const int32& BoneIndex, const int8& SkeletonToCheck) const;
#endif

private:

	/** Only true once Initialize() has successfully completed.*/
	bool bIsInitialized = false;

	/** true when roots are able to be retargeted */
	bool bRootsInitialized = false;

	/** true when at least one pair of bone chains is able to be retargeted */
	bool bAtLeastOneValidBoneChainPair = false;

	/** true when roots are able to be retargeted */
	bool bIKRigInitialized = false;

	/** The source asset this processor was initialized with. */
	UIKRetargeter* RetargeterAsset = nullptr;

	/** The internal data structures used to represent the SOURCE skeleton / pose during retargeter.*/
	FRetargetSkeleton SourceSkeleton;

	/** The internal data structures used to represent the TARGET skeleton / pose during retargeter.*/
	FTargetSkeleton TargetSkeleton;

	/** The IK Rig processor for running IK on the target */
	UPROPERTY(Transient) // must be property to keep from being GC'd
	TObjectPtr<UIKRigProcessor> IKRigProcessor = nullptr;

	/** The Source/Target pairs of Bone Chains retargeted using the FK algorithm */
	TArray<FRetargetChainPairFK> ChainPairsFK;

	/** The Source/Target pairs of Bone Chains retargeted using the IK Rig */
	TArray<FRetargetChainPairIK> ChainPairsIK;

	/** The Source/Target pair of Root Bones retargeted with scaled translation */
	FRootRetargeter RootRetargeter;
	
	/** Initializes the FRootRetargeter */
	bool InitializeRoots();

	/** Initializes the all the chain pairs */
	bool InitializeBoneChainPairs();

	/** Initializes the IK Rig that evaluates the IK solve for the target IK chains */
	bool InitializeIKRig(UObject* Outer, const USkeletalMesh* InSkeletalMesh);
	
	/** Internal retarget phase for the root. */
	void RunRootRetarget(const TArray<FTransform>& InGlobalTransforms, TArray<FTransform>& OutGlobalTransforms);

	/** Internal retarget phase for the FK chains. */
	void RunFKRetarget(const TArray<FTransform>& InGlobalTransforms, TArray<FTransform>& OutGlobalTransforms);

	/** Internal retarget phase for the IK chains. */
	void RunIKRetarget(
		const TArray<FTransform>& InSourceGlobalPose,
		TArray<FTransform>& OutTargeGlobalPose,
		const TMap<FName, float>& SpeedValuesFromCurves,
		const float DeltaTime);
};

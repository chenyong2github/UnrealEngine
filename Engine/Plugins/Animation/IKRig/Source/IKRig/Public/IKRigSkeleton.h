// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ReferenceSkeleton.h"

#include "IKRigSkeleton.generated.h"

class UIKRigDefinition;

USTRUCT()
struct IKRIG_API FIKRigSkeletonChain
{
	GENERATED_BODY()
	
	FIKRigSkeletonChain(){}
	FIKRigSkeletonChain(const FName& StartBone, const FName& EndBone): StartBone(StartBone), EndBone(EndBone){}

	FName StartBone;
	FName EndBone;
};

USTRUCT()
struct IKRIG_API FIKRigSkeleton
{
	GENERATED_BODY()

	/** Names of bones. Used to match hierarchy with runtime skeleton. */
	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	TArray<FName> BoneNames;

	/** Same length as BoneNames, stores index of parent for each bone. */
	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	TArray<int32> ParentIndices;

	/** Sparse array of bones that are to be excluded from any solvers (parented around, treated as FK children). */
	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	TArray<FName> ExcludedBones;

	/** The current GLOBAL space pose of each bone. */
	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	TArray<FTransform> CurrentPoseGlobal;

	/** The current LOCAL space pose of each bone. */
	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	TArray<FTransform> CurrentPoseLocal;

	/** The initial/reference GLOBAL space pose of each bone. */
	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	TArray<FTransform> RefPoseGlobal;

	void Initialize(const FReferenceSkeleton& RefSkeleton, const TArray<FName>& ExcludedBones);

	void Reset();
	
	int32 GetBoneIndexFromName(const FName InName) const;

	FName GetBoneNameFromIndex(const int32 BoneIndex) const;
	
	int32 GetParentIndex(const int32 BoneIndex) const;

	int32 GetParentIndexThatIsNotExcluded(const int32 BoneIndex) const;

	bool CopyPosesFromRefSkeleton(const FReferenceSkeleton& RefSkeleton);

	static void ConvertLocalPoseToGlobal(
		const TArray<int32>& InParentIndices,
		const TArray<FTransform>& InLocalPose,
		TArray<FTransform>& OutGlobalPose);
	
	void UpdateAllGlobalTransformFromLocal();
	
	void UpdateAllLocalTransformFromGlobal();
	
	void UpdateGlobalTransformFromLocal(const int32 BoneIndex);
	
	void UpdateLocalTransformFromGlobal(const int32 BoneIndex);
	
	void PropagateGlobalPoseBelowBone(const int32 BoneIndex);

	bool IsBoneInDirectLineage(const FName& Child, const FName& PotentialParent) const;

	bool IsBoneExcluded(const int32 BoneIndex) const;

	static void NormalizeRotations(TArray<FTransform>& Transforms);

	void GetChainsInList(const TArray<int32>& SelectedBones, TArray<FIKRigSkeletonChain>& OutChains) const;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ReferenceSkeleton.h"

#include "IKRigSkeleton.generated.h"

USTRUCT()
struct IKRIG_API FIKRigSkeleton
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	TArray<FName> BoneNames;
	
	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	TArray<int32> ParentIndices;
	
	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	TArray<FTransform> CurrentPoseGlobal;
	
	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	TArray<FTransform> CurrentPoseLocal;
	
	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	TArray<FTransform> RefPoseGlobal;

	void Initialize(const FReferenceSkeleton& RefSkeleton);
	
	int32 GetBoneIndexFromName(const FName InName) const;
	
	int32 GetParentIndex(const int32 BoneIndex) const;

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

	static void NormalizeRotations(TArray<FTransform>& Transforms);
};


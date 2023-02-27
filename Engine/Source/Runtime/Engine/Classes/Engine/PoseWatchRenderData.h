// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITORONLY_DATA

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "BoneIndices.h"
#include "Animation/AnimCurveTypes.h"

struct FReferenceSkeleton;

struct ENGINE_API FAnimNodePoseWatch
{
public:
	FAnimNodePoseWatch();

	// Object (anim instance) that this pose came from
	const UObject* Object;
	class UPoseWatch* PoseWatch;
	class UPoseWatchPoseElement* PoseWatchPoseElement;
	int32 NodeID;

	bool IsValid() const;

	void SetCurves(const FBlendedCurve& InCurves);

	template<class TAllocator>
	bool SetPose(const TArray<FBoneIndexType>& InRequiredBones, const TArray<FTransform, TAllocator>& InBoneTransforms)
	{
		RequiredBones = InRequiredBones;
		BoneTransforms = InBoneTransforms;
		return true;
	}

	void SetWorldTransform(const FTransform& InWorldTransform);

	/**
	 * Take a snapshot of the pose watch properties that this struct represents
	 * to be used when drawing the debug skeleton
	 */
	void CopyPoseWatchData(const FReferenceSkeleton& RefSkeleton);

	const TArray<FBoneIndexType>& GetRequiredBones() const;

	const TArray<FTransform>& GetBoneTransforms() const;

	const FBlendedHeapCurve& GetCurves() const;
	
	const FTransform& GetWorldTransform() const;

	FLinearColor GetBoneColor() const;

	FVector GetViewportOffset() const;

	const TArray<int32>& GetViewportAllowList() const;

	const TArray<int32>& GetParentIndices() const;

private:
	FTransform WorldTransform;
	TArray<FBoneIndexType> RequiredBones;
	TArray<FTransform> BoneTransforms;
	FBlendedHeapCurve Curves;
	
	// Mirrored properties updated on CopyPoseWatchData
	FLinearColor BoneColor;
	FVector ViewportOffset;
	TArray<int32> ViewportMaskAllowedList;
	TArray<int32> ParentIndices;
};

#endif
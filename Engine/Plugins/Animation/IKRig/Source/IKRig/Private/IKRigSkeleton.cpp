// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigSkeleton.h"

void FIKRigSkeleton::Initialize(const FReferenceSkeleton& RefSkeleton)
{
	BoneNames.Reset();
	ParentIndices.Reset();
	CurrentPoseGlobal.Reset();
	CurrentPoseLocal.Reset();
	RefPoseGlobal.Reset();
	
	// copy names and parent indices into local storage
	TArray<FMeshBoneInfo> RawBoneInfo = RefSkeleton.GetRawRefBoneInfo();
	for (int32 BoneIndex=0; BoneIndex<RefSkeleton.GetRawBoneNum(); ++BoneIndex)
	{
		BoneNames.Add(RawBoneInfo[BoneIndex].Name);
		ParentIndices.Add(RawBoneInfo[BoneIndex].ParentIndex);	
	}

	// copy all the poses out of the ref skeleton
	CopyPosesFromRefSkeleton(RefSkeleton);
}

int32 FIKRigSkeleton::GetBoneIndexFromName(const FName InName) const
{
	for (int32 Index=0; Index<BoneNames.Num(); ++Index)
	{
		if (InName == BoneNames[Index])
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 FIKRigSkeleton::GetParentIndex(const int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex>ParentIndices.Num() || BoneIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return ParentIndices[BoneIndex];
}

bool FIKRigSkeleton::CopyPosesFromRefSkeleton(const FReferenceSkeleton& RefSkeleton)
{
	// get a compacted local ref pose based on the stored names
	TArray<FTransform> CompactRefPoseLocal;
	for (const FName& BoneName : BoneNames)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("IK Rig is running on a skeleton that is missing bone named, '%s'."), *BoneName.ToString());
			return false;
		}
		CompactRefPoseLocal.Add(RefSkeleton.GetRefBonePose()[BoneIndex]);
	}
	
	// copy local ref pose to global
	ConvertLocalPoseToGlobal(ParentIndices, CompactRefPoseLocal, RefPoseGlobal);

	// initialize current GLOBAL pose to reference pose
	CurrentPoseGlobal = RefPoseGlobal;

	// initialize current LOCAL pose
	UpdateAllLocalTransformFromGlobal();

	return true; // supplied ref skeleton had all the bones we are looking for
}

void FIKRigSkeleton::ConvertLocalPoseToGlobal(
	const TArray<int32>& InParentIndices,
	const TArray<FTransform>& InLocalPose,
	TArray<FTransform>& OutGlobalPose)
{
	check(InLocalPose.Num() == InParentIndices.Num());
	
	OutGlobalPose.Reset();
	for (int32 BoneIndex=0; BoneIndex<InLocalPose.Num(); ++BoneIndex)
	{
		const int32 ParentIndex = InParentIndices[BoneIndex];
		if (ParentIndex == INDEX_NONE)
		{
			OutGlobalPose.Add(InLocalPose[BoneIndex]);
			continue; // root bone is always in local space 
		}

		const FTransform& ChildLocalTransform  = InLocalPose[BoneIndex];
		const FTransform& ParentGlobalTransform  = OutGlobalPose[ParentIndex];
		OutGlobalPose.Add(ChildLocalTransform * ParentGlobalTransform);
	}
}

void FIKRigSkeleton::UpdateAllGlobalTransformFromLocal()
{
	// generate GLOBAL transforms based on LOCAL transforms
	CurrentPoseGlobal = CurrentPoseLocal;
	for (int32 BoneIndex=0; BoneIndex<CurrentPoseLocal.Num(); ++BoneIndex)
	{
		UpdateGlobalTransformFromLocal(BoneIndex);
	}
}

void FIKRigSkeleton::UpdateGlobalTransformFromLocal(const int32 BoneIndex)
{
	const int32 ParentIndex = ParentIndices[BoneIndex];
	if (ParentIndex == INDEX_NONE)
	{
		CurrentPoseGlobal[BoneIndex] = CurrentPoseLocal[BoneIndex]; // root bone is always in local space
		return; 
	}

	const FTransform& ChildLocalTransform  = CurrentPoseLocal[BoneIndex];
	const FTransform& ParentGlobalTransform  = CurrentPoseGlobal[ParentIndex];
	CurrentPoseGlobal[BoneIndex] = ChildLocalTransform * ParentGlobalTransform;
}

void FIKRigSkeleton::UpdateAllLocalTransformFromGlobal()
{
	// generate LOCAL transforms based on GLOBAL transforms
	CurrentPoseLocal = CurrentPoseGlobal;
	for (int32 BoneIndex=0; BoneIndex<CurrentPoseGlobal.Num(); ++BoneIndex)
	{
		UpdateLocalTransformFromGlobal(BoneIndex);
	}
}

void FIKRigSkeleton::UpdateLocalTransformFromGlobal(const int32 BoneIndex)
{
	const int32 ParentIndex = ParentIndices[BoneIndex];
	if (ParentIndex == INDEX_NONE)
	{
		CurrentPoseLocal[BoneIndex] = CurrentPoseGlobal[BoneIndex]; // root bone is always in local space
		return; 
	}

	const FTransform& ChildGlobal  = CurrentPoseGlobal[BoneIndex];
	const FTransform& ParentGlobal  = CurrentPoseGlobal[ParentIndex];
	CurrentPoseLocal[BoneIndex] = ChildGlobal.GetRelativeTransform(ParentGlobal);
}

void FIKRigSkeleton::PropagateGlobalPoseBelowBone(const int32 StartBoneIndex)
{
	for (int32 BoneIndex=StartBoneIndex+1; BoneIndex<CurrentPoseGlobal.Num(); ++BoneIndex)
	{
		UpdateGlobalTransformFromLocal(BoneIndex);
		UpdateLocalTransformFromGlobal(BoneIndex);
	}
}

void FIKRigSkeleton::NormalizeRotations(TArray<FTransform>& Transforms)
{
	for (FTransform& Transform : Transforms)
	{
		Transform.NormalizeRotation();
	}
}
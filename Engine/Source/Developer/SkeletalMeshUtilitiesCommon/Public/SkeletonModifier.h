// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/MathFwd.h"
#include "Containers/Array.h"
#include "Templates/UniquePtr.h"
#include "MeshDescription.h"
#include "ReferenceSkeleton.h"

#include "SkeletonModifier.generated.h"

class USkeletalMesh;
class FName;
struct FMeshDescription;
struct FReferenceSkeleton;
struct FTransformComposer;

USTRUCT(BlueprintType)
struct SKELETALMESHUTILITIESCOMMON_API FMirrorOptions
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Mirror")
	TEnumAsByte<EAxis::Type> MirrorAxis = EAxis::X;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Mirror")
	bool bMirrorRotation = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Mirror")
	FString LeftString = TEXT("_l");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Mirror")
	FString RightString = TEXT("_r");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Mirror")
	bool bMirrorChildren = true;

	FTransform MirrorTransform(const FTransform& InGlobalTransform) const;
	FVector MirrorVector(const FVector& InVector) const;
};

/**
 * FSkeletalMeshSkeletonModifier
 */

class SKELETALMESHUTILITIESCOMMON_API FSkeletonModifier
{
public:

	bool Init(USkeletalMesh* InSkeletalMesh);
	
	void ExternalUpdate(const FReferenceSkeleton& InRefSkeleton, const TArray<int32>& InIndexTracker);

	/** Creates a new bone in the skeleton hierarchy at desired transform
	 *  @param InBoneName The new bone's name.
	 *  @param InParentName The new bone parent's name. 
	 *  @param InTransform The default local transform in the parent space.
	 *  @return \c true if the operation succeeded, false otherwise. 
	 */
	bool AddBone(const FName InBoneName, const FName InParentName, const FTransform& InTransform);
	bool AddBones(const TArray<FName>& InBonesName, const TArray<FName>& InParentsName, const TArray<FTransform>& InTransforms);

	/** Mirror bones
	 *  @param InBoneName The new bone's name.
	 *  @param InOptions The mirroring options
	 *  @return \c true if the operation succeeded, false otherwise. 
	 */
	bool MirrorBone(const FName InBoneName, const FMirrorOptions& InOptions = FMirrorOptions());
	bool MirrorBones(const TArray<FName>& InBonesName, const FMirrorOptions& InOptions = FMirrorOptions());
	bool MirrorBonesDep(const TArray<FName>& InBonesName);

	/** Moves a bone at a new desired local transform
	 *  @param InBoneToMove The new bone's name that needs to be moved.
	 *  @param InNewTransform The new local transform in the bone's parent space.
	 *  @param bMoveChildren Propagate new transform to children
	 *  @return \c true if the operation succeeded, false otherwise. 
	 */
	bool MoveBone(const FName InBoneToMove, const FTransform& InNewTransform, const bool bMoveChildren);
	bool MoveBones(const TArray<FName>& InBonesToMove, const TArray<FTransform>& InNewTransforms, const bool bMoveChildren);

	/** Remove a bone in the skeleton hierarchy
	 *  @param InBoneName The new bone's name.
	 *  @param bRemoveChildren Remove children recursively.
	 *  @return \c true if the operation succeeded, false otherwise. 
	 */
	bool RemoveBone(const FName InBoneName, const bool bRemoveChildren);
	bool RemoveBones(const TArray<FName>& InBoneNames, const bool bRemoveChildren);

	/** Rename bones
	 *  @param InOldBoneName The current bone's name.
	 *  @param InNewBoneName The new bone's name.
	 *  @return \c true if the operation succeeded, false otherwise. 
	 */
	bool RenameBone(const FName InOldBoneName, const FName InNewBoneName);
	bool RenameBones(const TArray<FName>& InOldBoneNames, const TArray<FName>& InNewBoneNames);
	
	/** Parent bones
	 *  @param InBoneName The current bone's name.
	 *  @param InParentName The new parent's name (Name_NONE to unparent).
	 *  @return \c true if the operation succeeded, false otherwise. 
	 */
	bool ParentBone(const FName InBoneName, const FName InParentName);
	bool ParentBones(const TArray<FName>& InBoneNames, const TArray<FName>& InParentNames);

	/** Align bones
	 *  @param InBoneName The current bone's name.
	 *  @param bAlignChildren
	 *  @return \c true if the operation succeeded, false otherwise. 
	 */
	bool AlignBone(const FName InBoneName, const bool bAlignChildren);
	bool AlignBones(const TArray<FName>& InBoneNames, const bool bAlignChildren);
	
	/**
	 * Actually applies the skeleton modifications to the skeletal mesh.
	 * @return true if commit succeeded.
	 */
	bool CommitSkeletonToSkeletalMesh();

	FName GetUniqueName(const FName InBoneName) const;
	
	const FReferenceSkeleton& GetReferenceSkeleton() const;
	const TArray<int32>& GetBoneIndexTracker() const;
	const FTransform& GetTransform(const int32 InBoneIndex, const bool bGlobal = false) const;
	
private:

	bool IsReferenceSkeletonValid(const bool bLog = true) const;

	void UpdateBoneTracker(const TArray<FMeshBoneInfo>& InOtherInfos);
	
	// mirroring functions
	void GetBonesToMirror(const TArray<FName>& InBonesName, const FMirrorOptions& InOptions, TArray<int32>& OutBonesToMirror) const;
	void GetMirroredNames(const TArray<int32>& InBonesToMirror, const FMirrorOptions& InOptions, TArray<FName>& OutBonesName) const;
	void GetMirroredBones(const TArray<int32>& InBonesToMirror, const TArray<FName>& InMirroredNames, TArray<int32>& OutMirroredBones);
	void GetMirroredTransforms(const TArray<int32>& InBonesToMirror, const FMirrorOptions& InOptions, TArray<FTransform>& OutMirroredTransforms) const;
	
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;
	TUniquePtr<FMeshDescription> MeshDescription;
	TUniquePtr<FReferenceSkeleton> ReferenceSkeleton;
	TUniquePtr<FTransformComposer> TransformComposer;
	TArray<int32> BoneIndexTracker;
};

struct FTransformComposer
{
	FTransformComposer(const FReferenceSkeleton& InRefSkeleton)
		: RefSkeleton(InRefSkeleton)
		, Transforms(InRefSkeleton.GetRawRefBonePose())
	{
		TransformCached.Init(false, Transforms.Num());
	}

	const FTransform& GetGlobalTransform(const uint32 Index)
	{
		if (!Transforms.IsValidIndex(Index))
		{
			return FTransform::Identity;
		}

		if (TransformCached[Index])
		{
			return Transforms[Index];
		}

		const int32 ParentIndex = RefSkeleton.GetRawParentIndex(Index);
		if (ParentIndex > INDEX_NONE)
		{
			Transforms[Index] *= GetGlobalTransform(ParentIndex);
		}

		TransformCached[Index] = true;
		return Transforms[Index];
	}

	void Invalidate(const uint32 Index = INDEX_NONE)
	{
		if (!TransformCached.IsValidIndex(Index))
		{
			Transforms = RefSkeleton.GetRawRefBonePose();
			TransformCached.Init(false, Transforms.Num());
			return;
		}
		
		Transforms[Index] = RefSkeleton.GetRawRefBonePose()[Index];
		TransformCached[Index] = false;

		TArray<int32> Children; RefSkeleton.GetDirectChildBones(Index, Children);
		for (const int32 ChildIndex: Children)
		{
			Invalidate(ChildIndex);
		}
	}

	const FReferenceSkeleton& RefSkeleton;
	TArray<FTransform> Transforms;
	TBitArray<> TransformCached;
};


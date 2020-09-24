// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigHierarchyPose.h"
#include "ReferenceSkeleton.h"
#include "RigBoneHierarchy.generated.h"

class UControlRig;

UENUM(BlueprintType)
enum class ERigBoneType : uint8
{
	Imported,
	User
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigBone: public FRigElement
{
	GENERATED_BODY()

	FRigBone()
		: FRigElement()
		, ParentName(NAME_None)
		, ParentIndex(INDEX_NONE)
		, InitialTransform(FTransform::Identity)
		, GlobalTransform(FTransform::Identity)
		, LocalTransform(FTransform::Identity)
		, Dependents()
		, Type(ERigBoneType::Imported)
	{
	}
	virtual ~FRigBone() {}

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = FRigElement)
	FName ParentName;

	UPROPERTY(transient)
	int32 ParentIndex;

	/* Initial global transform that is saved in this rig */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = FRigElement)
	FTransform InitialTransform;

	UPROPERTY(BlueprintReadOnly, transient, EditAnywhere, Category = FRigElement)
	FTransform GlobalTransform;

	UPROPERTY(BlueprintReadOnly, transient, EditAnywhere, Category = FRigElement)
	FTransform LocalTransform;

	/** dependent list - direct dependent for child or anything that needs to update due to this */
	UPROPERTY(transient)
	TArray<int32> Dependents;

	/** the source of the bone to differentiate procedurally generated, imported etc */
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = FRigElement)
	ERigBoneType Type;

	FORCEINLINE virtual ERigElementType GetElementType() const override
	{
		return ERigElementType::Bone;
	}

	FORCEINLINE virtual FRigElementKey GetParentElementKey(bool bForce = false) const
	{
		if (ParentIndex != INDEX_NONE || bForce)
		{
			return FRigElementKey(ParentName, GetElementType());
		}
		return FRigElementKey();
	}
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigBoneHierarchy
{
	GENERATED_BODY()

	FRigBoneHierarchy();
	FRigBoneHierarchy& operator= (const FRigBoneHierarchy &InOther);

	FORCEINLINE ERigElementType RigElementType() const { return ERigElementType::Bone; }

	FORCEINLINE int32 Num() const { return Bones.Num(); }
	FORCEINLINE const FRigBone& operator[](int32 InIndex) const { return Bones[InIndex]; }
	FORCEINLINE FRigBone& operator[](int32 InIndex) { return Bones[InIndex]; }
	FORCEINLINE const FRigBone& operator[](const FName& InName) const { return Bones[GetIndex(InName)]; }
	FORCEINLINE FRigBone& operator[](const FName& InName) { return Bones[GetIndex(InName)]; }

	FORCEINLINE TArray<FRigBone>::RangedForIteratorType      begin()       { return Bones.begin(); }
	FORCEINLINE TArray<FRigBone>::RangedForConstIteratorType begin() const { return Bones.begin(); }
	FORCEINLINE TArray<FRigBone>::RangedForIteratorType      end()         { return Bones.end();   }
	FORCEINLINE TArray<FRigBone>::RangedForConstIteratorType end() const   { return Bones.end();   }

	FORCEINLINE bool IsNameAvailable(const FName& InPotentialNewName) const { return GetIndex(InPotentialNewName) == INDEX_NONE; }

	FName GetSafeNewName(const FName& InPotentialNewName) const;

	FRigBone& Add(const FName& InNewName, const FName& InParentName = NAME_None, ERigBoneType InType = ERigBoneType::Imported, const FTransform& InInitTransform = FTransform::Identity);

	FRigBone& Add(const FName& InNewName, const FName& InParentName, ERigBoneType InType, const FTransform& InInitTransform, const FTransform& InLocalTransform, const FTransform& InGlobalTransform);

	FRigBone Remove(const FName& InNameToRemove);

	FName Rename(const FName& InOldName, const FName& InNewName);

	bool Reparent(const FName& InName, const FName& InNewParentName);

	FName GetName(int32 InIndex) const;

	FORCEINLINE int32 GetIndex(const FName& InName) const
	{
		if(NameToIndexMapping.Num() != Bones.Num())
		{
			return GetIndexSlow(InName);
		}

		const int32* Index = NameToIndexMapping.Find(InName);
		if (Index)
		{
			return *Index;
		}

		return INDEX_NONE;
	}

	// list of names of children - this is not cheap, and is supposed to be used only for one time set up
	int32 GetChildren(const FName& InName, TArray<int32>& OutChildren, bool bRecursively) const;

	int32 GetChildren(const int32 InIndex, TArray<int32>& OutChildren, bool bRecursively) const;

	void SetGlobalTransform(const FName& InName, const FTransform& InTransform, bool bPropagateTransform = true);

	void SetGlobalTransform(int32 InIndex, const FTransform& InTransform, bool bPropagateTransform = true);

	FTransform GetGlobalTransform(const FName& InName) const;

	FTransform GetGlobalTransform(int32 InIndex) const;

	void SetLocalTransform(const FName& InName, const FTransform& InTransform, bool bPropagateTransform = true);

	void SetLocalTransform(int32 InIndex, const FTransform& InTransform, bool bPropagateTransform = true);

	FTransform GetLocalTransform(const FName& InName) const;

	FTransform GetLocalTransform(int32 InIndex) const;

	void SetInitialGlobalTransform(const FName& InName, const FTransform& InTransform, bool bPropagateTransform = false);

	void SetInitialGlobalTransform(int32 InIndex, const FTransform& InTransform, bool bPropagateTransform = false);

	void SetInitialLocalTransform(const FName& InName, const FTransform& InTransform, bool bPropagateTransform = false);

	void SetInitialLocalTransform(int32 InIndex, const FTransform& InTransform, bool bPropagateTransform = false);

	FTransform GetInitialGlobalTransform(const FName& InName) const;

	FTransform GetInitialGlobalTransform(int32 InIndex) const;

	FTransform GetInitialLocalTransform(const FName& InName) const;

	FTransform GetInitialLocalTransform(int32 InIndex) const;

	// updates all of the internal caches
	void Initialize(bool bResetTransforms = true);

	// clears the hierarchy and removes all content
	void Reset();

	// returns the current pose
	FRigPose GetPose() const;

	// sets the current transforms from the given pose
	void SetPose(FRigPose& InPose);

	// resets all of the transforms back to the initial transform
	void ResetTransforms();

	// copies all initial transforms from another hierarchy
	void CopyInitialTransforms(const FRigBoneHierarchy& InOther);

	// recomputes all of the global transforms from local
	void RecomputeGlobalTransforms();

	// recomputes the local transform of a single bone
	void RecalculateLocalTransform(int32 InIndex);

	// recomputes the global transform of a single bone
	void RecalculateGlobalTransform(int32 InIndex);

	// propagates the transform change for a single bone
	void PropagateTransform(int32 InIndex);

	// import skeleton
	TArray<FRigElementKey> ImportSkeleton(const FReferenceSkeleton& InSkeleton, const FName& InNameSpace, bool bReplaceExistingBones, bool bRemoveObsoleteBones, bool bSelectBones, bool bNotify);

	bool Select(const FName& InName, bool bSelect = true);
	bool ClearSelection();
	TArray<FName> CurrentSelection() const;
	bool IsSelected(const FName& InName) const;

	FRigElementSelected OnBoneSelected;

#if WITH_EDITOR

	FRigElementAdded OnBoneAdded;
	FRigElementRemoved OnBoneRemoved;
	FRigElementRenamed OnBoneRenamed;
	FRigElementReparented OnBoneReparented;

#endif

private:

	// disable copy constructor
	FRigBoneHierarchy(const FRigBoneHierarchy& InOther) {}

	FRigHierarchyContainer* Container;

	UPROPERTY(EditAnywhere, Category = FRigBoneHierarchy)
	TArray<FRigBone> Bones;

	// can serialize fine? 
	UPROPERTY()
	TMap<FName, int32> NameToIndexMapping;

	UPROPERTY(transient)
	TArray<FName> Selection;

	int32 GetIndexSlow(const FName& InName) const;

	void RecalculateLocalTransform(FRigBone& InOutBone);
	void RecalculateGlobalTransform(FRigBone& InOutBone);

	void RefreshParentNames();
	void RefreshMapping();
	void AppendToPose(FRigPose& InOutPose) const;
	void Sort();

	// list of names of children - this is not cheap, and is supposed to be used only for one time set up
	int32 GetChildrenRecursive(const int32 InIndex, TArray<int32>& OutChildren, bool bRecursively) const;

	bool bSuspendNotifications;

	friend struct FRigHierarchyContainer;
	friend struct FCachedRigElement;
	friend class UControlRigHierarchyModifier;
	friend class UControlRig;
};

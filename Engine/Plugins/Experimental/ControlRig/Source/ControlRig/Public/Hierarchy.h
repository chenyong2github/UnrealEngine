// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/StatsHierarchical.h"
#include "Hierarchy.generated.h"

class UControlRig;

USTRUCT(BlueprintType)
struct FRigBone
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = FRigHierarchy)
	FName Name;

	UPROPERTY(VisibleAnywhere, Category = FRigHierarchy)
	FName ParentName;

	UPROPERTY(transient)
	int32 ParentIndex;

	/* Initial global transform that is saved in this rig */
	UPROPERTY(EditAnywhere, Category = FRigHierarchy)
	FTransform InitialTransform;

	UPROPERTY(transient, VisibleAnywhere, Category = FRigHierarchy)
	FTransform GlobalTransform;

	UPROPERTY(transient, VisibleAnywhere, Category = FRigHierarchy)
	FTransform LocalTransform;

	/** dependent list - direct dependent for child or anything that needs to update due to this */
	UPROPERTY(transient)
	TArray<int32> Dependents;
};

USTRUCT()
struct CONTROLRIG_API FRigHierarchy
{
	GENERATED_BODY()

private:
	UPROPERTY(EditAnywhere, Category = FRigHierarchy)
	TArray<FRigBone> Bones;

	// can serialize fine? 
	UPROPERTY()
	TMap<FName, int32> NameToIndexMapping;

public:

	const TArray<FRigBone>& GetBones() const 
	{ 
		return Bones; 
	}

	void AddBone(const FName& NewBoneName, const FName& Parent, const FTransform& InitTransform)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		int32 ParentIndex = GetIndex(Parent);
		bool bHasParent = (ParentIndex != INDEX_NONE);

		FRigBone NewBone;
		NewBone.Name = NewBoneName;
		NewBone.ParentIndex = ParentIndex;
		NewBone.ParentName = bHasParent? Parent : NAME_None;
		NewBone.InitialTransform = InitTransform;
		NewBone.GlobalTransform = InitTransform;
		RecalculateLocalTransform(NewBone);

		Bones.Add(NewBone);
		RefreshMapping();
	}

	void AddBone(const FName& NewBoneName, const FName& Parent, const FTransform& InitTransform, const FTransform& LocalTransform, const FTransform& GlobalTransform)
	{
		AddBone(NewBoneName, Parent, InitTransform);

		int32 NewIndex = GetIndex(NewBoneName);
		Bones[NewIndex].LocalTransform = LocalTransform;
		Bones[NewIndex].GlobalTransform = GlobalTransform;
	}

	void Reparent(const FName& InBone, const FName& NewParent)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		int32 Index = GetIndex(InBone);
		// can't parent to itself
		if (Index != INDEX_NONE && InBone != NewParent)
		{
			// should allow reparent to none (no parent)
			// if invalid, we consider to be none
			int32 ParentIndex = GetIndex(NewParent);
			bool bHasParent = (ParentIndex != INDEX_NONE);
			FRigBone& CurBone = Bones[Index];
			CurBone.ParentIndex = ParentIndex;
			CurBone.ParentName = (bHasParent)? NewParent : NAME_None;
			RecalculateLocalTransform(CurBone);

			// we want to make sure parent is before the child
			RefreshMapping();
		}
	}

	void DeleteBone(const FName& BoneToDelete, bool bIncludeChildren)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		TArray<int32> Children;
		if (GetChildren(BoneToDelete, Children, true) > 0)
		{
			// sort by child index
			Children.Sort([](const int32& A, const int32& B) { return A < B; });

			// want to delete from end to the first 
			for (int32 ChildIndex = Children.Num() - 1; ChildIndex >= 0; --ChildIndex)
			{
				Bones.RemoveAt(Children[ChildIndex]);
			}
		}

		// in theory, since I'm not adding new element here
		// it is safe to do search using FindIndex, but it may cause
		// difficulty in the future, so I'm changing to FindIndexSlow
		// note that we're removing units here, but the index is removed from later
		// to begin, so in theory it should be fine
		int32 IndexToDelete = GetIndex(BoneToDelete);
		Bones.RemoveAt(IndexToDelete);

		RefreshMapping();
	}

	FName GetParentName(const FName& InBone) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		int32 Index = GetIndex(InBone);
		if (Index != INDEX_NONE)
		{
			return Bones[Index].ParentName;
		}

		return NAME_None;
	}

	int32 GetParentIndex(const int32 BoneIndex) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (BoneIndex != INDEX_NONE)
		{
			return Bones[BoneIndex].ParentIndex;
		}

		return INDEX_NONE;
	}
	// list of names of children - this is not cheap, and is supposed to be used only for one time set up
	int32 GetChildren(const FName& InBone, TArray<int32>& OutChildren, bool bRecursively) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		return GetChildren(GetIndex(InBone), OutChildren, bRecursively);
	}

	int32 GetChildren(const int32 InBoneIndex, TArray<int32>& OutChildren, bool bRecursively) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		OutChildren.Reset();

		if (InBoneIndex != INDEX_NONE)
		{
			GetChildrenRecursive(InBoneIndex, OutChildren, bRecursively);
		}

		return OutChildren.Num();
	}

	FName GetName(int32 Index) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (Bones.IsValidIndex(Index))
		{
			return Bones[Index].Name;
		}

		return NAME_None;
	}

	int32 GetIndex(const FName& Bone) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		// ensure if it does not match
		//ensureAlways(Bones.Num() == NameToIndexMapping.Num());

		const int32* Index = NameToIndexMapping.Find(Bone);
		if (Index)
		{
			return *Index;
		}

		return INDEX_NONE;
	}

//#if WITH_EDITOR
	// @FIXMELINA: figure out how to remove this outside of editor
	// ignore mapping, run slow search
	// this is useful in editor while editing
	// we don't want to build mapping data every time
	int32 GetIndexSlow(const FName& Bone) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		for (int32 BoneId = 0; BoneId < Bones.Num(); ++BoneId)
		{
			if (Bones[BoneId].Name == Bone)
			{
				return BoneId;
			}
		}

		return INDEX_NONE;
	}
//#endif // WITH_EDITOR
	void SetGlobalTransform(const FName& Bone, const FTransform& InTransform, bool bPropagateTransform = true)
	{
		SetGlobalTransform(GetIndex(Bone), InTransform, bPropagateTransform);
	}

	void SetGlobalTransform(int32 Index, const FTransform& InTransform, bool bPropagateTransform = true)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (Bones.IsValidIndex(Index))
		{
			FRigBone& Bone = Bones[Index];
			Bone.GlobalTransform = InTransform;
			Bone.GlobalTransform.NormalizeRotation();
			RecalculateLocalTransform(Bone);

			if (bPropagateTransform)
			{
				PropagateTransform(Index);
			}
		}
	}

	FTransform GetGlobalTransform(const FName& Bone) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		return GetGlobalTransform(GetIndex(Bone));
	}

	FTransform GetGlobalTransform(int32 Index) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (Bones.IsValidIndex(Index))
		{
			return Bones[Index].GlobalTransform;
		}

		return FTransform::Identity;
	}

	void SetLocalTransform(const FName& Bone, const FTransform& InTransform, bool bPropagateTransform = true)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		SetLocalTransform(GetIndex(Bone), InTransform, bPropagateTransform);
	}

	void SetLocalTransform(int32 Index, const FTransform& InTransform, bool bPropagateTransform = true)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (Bones.IsValidIndex(Index))
		{
			FRigBone& Bone = Bones[Index];
			Bone.LocalTransform = InTransform;
			RecalculateGlobalTransform(Bone);

			if (bPropagateTransform)
			{
				PropagateTransform(Index);
			}
		}
	}

	FTransform GetLocalTransform(const FName& Bone) const
	{
		return GetLocalTransform(GetIndex(Bone));
	}

	FTransform GetLocalTransform(int32 Index) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (Bones.IsValidIndex(Index))
		{
			return Bones[Index].LocalTransform;
		}

		return FTransform::Identity;
	}

	void SetInitialTransform(const FName& Bone, const FTransform& InTransform)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		SetInitialTransform(GetIndex(Bone), InTransform);
	}

	void SetInitialTransform(int32 Index, const FTransform& InTransform)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (Bones.IsValidIndex(Index))
		{
			FRigBone& Bone = Bones[Index];
			Bone.InitialTransform = InTransform;
			Bone.InitialTransform.NormalizeRotation();
			RecalculateLocalTransform(Bone);
		}
	}

	FTransform GetInitialTransform(const FName& Bone) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		return GetInitialTransform(GetIndex(Bone));
	}

	FTransform GetInitialTransform(int32 Index) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (Bones.IsValidIndex(Index))
		{
			return Bones[Index].InitialTransform;
		}

		return FTransform::Identity;
	}
	// @todo: move to private
	void RecalculateLocalTransform(FRigBone& InOutBone)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		bool bHasParent = InOutBone.ParentIndex != INDEX_NONE;
		InOutBone.LocalTransform = (bHasParent) ? InOutBone.GlobalTransform.GetRelativeTransform(Bones[InOutBone.ParentIndex].GlobalTransform) : InOutBone.GlobalTransform;
	}

	// @todo: move to private
	void RecalculateGlobalTransform(FRigBone& InOutBone)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		bool bHasParent = InOutBone.ParentIndex != INDEX_NONE;
		InOutBone.GlobalTransform = (bHasParent) ? InOutBone.LocalTransform * Bones[InOutBone.ParentIndex].GlobalTransform : InOutBone.LocalTransform;
	}

	void Rename(const FName& OldName, const FName& NewName)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (OldName != NewName)
		{
			const int32 Found = GetIndex(OldName);
			if (Found != INDEX_NONE)
			{
				Bones[Found].Name = NewName;

				// go through find all children and rename them
				for (int32 Index = 0; Index < Bones.Num(); ++Index)
				{
					if (Bones[Index].ParentName == OldName)
					{
						Bones[Index].ParentName = NewName;
					}
				}

				RefreshMapping();
			}
		}
	}

	// updates all of the internal caches
	void Initialize();

	// clears the hierarchy and removes all content
	void Reset();

	// resets all of the transforms back to the initial transform
	void ResetTransforms();

	int32 GetNum() const
	{
		return Bones.Num();
	}
private:
	void RefreshMapping();
	void Sort();

	// list of names of children - this is not cheap, and is supposed to be used only for one time set up
	int32 GetChildrenRecursive(const int32 InBoneIndex, TArray<int32>& OutChildren, bool bRecursively) const
	{
		const int32 StartChildIndex = OutChildren.Num();

		// all children should be later than parent
		for (int32 ChildIndex = InBoneIndex + 1; ChildIndex < Bones.Num(); ++ChildIndex)
		{
			if (Bones[ChildIndex].ParentIndex == InBoneIndex)
			{
				OutChildren.AddUnique(ChildIndex);
			}
		}

		if (bRecursively)
		{
			// since we keep appending inside of functions, we make sure not to go over original list
			const int32 EndChildIndex = OutChildren.Num() - 1;
			for (int32 ChildIndex = StartChildIndex; ChildIndex <= EndChildIndex; ++ChildIndex)
			{
				GetChildrenRecursive(OutChildren[ChildIndex], OutChildren, bRecursively);
			}
		}

		return OutChildren.Num();
	}

	void PropagateTransform(int32 BoneIndex);

	friend FRigHierarchyRef;
};

USTRUCT()
struct FRigHierarchyContainer
{
	GENERATED_BODY()

	// index to hierarchy
	TMap<FName, uint32> MapContainer;

	// list of hierarchies
	TArray<FRigHierarchy> Hierarchies;

	// base hierarchy
	// this should serialize
	UPROPERTY()
	FRigHierarchy BaseHierarchy;

	FRigHierarchy* Find(const FName& InName)
	{
		uint32* IndexPtr = MapContainer.Find(InName);
		if (IndexPtr && Hierarchies.IsValidIndex(*IndexPtr))
		{
			return &Hierarchies[*IndexPtr];
		}

		return nullptr;
	}

	void Reset()
	{
		BaseHierarchy.Reset();

		// @todo reset whole hierarhcy
	}

	void ResetTransforms()
	{
		BaseHierarchy.ResetTransforms();

		// @todo reset whole hierarhcy
	}
};

USTRUCT()
struct FRigHierarchyRef
{
	GENERATED_BODY()

	FRigHierarchyRef()
		: Container(nullptr)
		, bUseBaseHierarchy(true)
	{

	}

	FRigHierarchy* Get() 
	{
		return GetInternal();
	}

	const FRigHierarchy* Get() const
	{
		return (const FRigHierarchy*)GetInternal();
	}

	FRigHierarchy* Find(const FName& InName)
	{
		if(Container)
		{
			return Container->Find(InName);
		}
		return nullptr;
	}

	// create name if the name isn't set, for now it only accepts root
	bool CreateHierarchy(const FName& RootName, const FRigHierarchyRef& SourceHierarchyRef);
	bool MergeHierarchy(const FRigHierarchyRef& SourceHierarchyRef);

private:
	struct FRigHierarchyContainer* Container;

	// @todo: this only works with merge right now. Should fix for all cases
	UPROPERTY(EditAnywhere, Category = FRigHierarchyRef)
	bool bUseBaseHierarchy; 

	/** Name of Hierarchy */
	UPROPERTY(EditAnywhere, Category = FRigHierarchyRef, meta=(EditCondition = "!bUseBaseHierarchy"))
	FName Name;

	// utility functions
	bool CreateHierarchy(const FName& RootName, const FRigHierarchy* SourceHierarchy);
	bool MergeHierarchy(const FRigHierarchy* InSourceHierarchy);

	FRigHierarchy* GetInternal() const
	{
		if (Container)
		{
			if (bUseBaseHierarchy)
			{
				return &Container->BaseHierarchy;
			}

			return Container->Find(Name);
		}
		return nullptr;
	}
	friend class UControlRig;
	friend class FControlRigUnitTestBase;
};
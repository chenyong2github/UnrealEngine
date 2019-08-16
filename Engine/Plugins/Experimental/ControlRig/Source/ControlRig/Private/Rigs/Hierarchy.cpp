// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Hierarchy.h"
#include "ControlRig.h"
#include "HelperUtil.h"

////////////////////////////////////////////////////////////////////////////////
// FRigHierarchy
////////////////////////////////////////////////////////////////////////////////

void FRigHierarchy::Sort()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	TMap<int32, TArray<int32>> HierarchyTree;

	TArray<int32> SortedArray;

	// first figure out children and find roots
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		int32 ParentIndex = GetIndexSlow(Bones[Index].ParentName);
		if (ParentIndex != INDEX_NONE)
		{
			TArray<int32>& ChildIndices = HierarchyTree.FindOrAdd(ParentIndex);
			ChildIndices.Add(Index);
		}
		else
		{
			// as as a root
			HierarchyTree.Add(Index);
			// add them to the list first
			SortedArray.Add(Index);
		}
	}

	// now go through map and add to sorted array
	for (int32 SortIndex = 0; SortIndex < SortedArray.Num(); ++SortIndex)
	{
		// add children of sorted array
		TArray<int32>* ChildIndices = HierarchyTree.Find(SortedArray[SortIndex]);
		if (ChildIndices)
		{
			// append children now
			SortedArray.Append(*ChildIndices);
			// now sorted array will grow
			// this is same as BFS as it starts from all roots, and going down
		}
	}

	check(SortedArray.Num() == Bones.Num());

	// create new list with sorted
	TArray<FRigBone> NewSortedList;
	NewSortedList.AddDefaulted(Bones.Num());
	for (int32 NewIndex = 0; NewIndex < SortedArray.Num(); ++NewIndex)
	{
		NewSortedList[NewIndex] = Bones[SortedArray[NewIndex]];
	}

	Bones = MoveTemp(NewSortedList);

	// now fix up parent Index
	for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
	{
		Bones[BoneIndex].ParentIndex = GetIndexSlow(Bones[BoneIndex].ParentName);
		// parent index always should be less than this index, even if invalid
		check(Bones[BoneIndex].ParentIndex < BoneIndex);
	}
}

void FRigHierarchy::RefreshMapping()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Sort();

	NameToIndexMapping.Empty();
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		NameToIndexMapping.Add(Bones[Index].Name, Index);
	}
}

void FRigHierarchy::Initialize()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	
	RefreshMapping();

	// update parent index
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		Bones[Index].ParentIndex = GetIndex(Bones[Index].ParentName);
	}

	// initialize transform
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		Bones[Index].GlobalTransform = Bones[Index].InitialTransform;
		RecalculateLocalTransform(Bones[Index]);

		// update children
		GetChildren(Index, Bones[Index].Dependents, false);
	}
}

void FRigHierarchy::Reset()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Bones.Reset();
}

void FRigHierarchy::ResetTransforms()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// initialize transform
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		Bones[Index].GlobalTransform = Bones[Index].InitialTransform;
		RecalculateLocalTransform(Bones[Index]);
	}
}

void FRigHierarchy::PropagateTransform(int32 BoneIndex)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<int32> Dependents = Bones[BoneIndex].Dependents;
	for (int32 DependentIndex = 0; DependentIndex<Dependents.Num(); ++DependentIndex)
	{
		int32 Index = Dependents[DependentIndex];
		RecalculateGlobalTransform(Bones[Index]);
		PropagateTransform(Index);
	}
}
////////////////////////////////////////////////////////////////////////////////
// FRigHierarchyRef
////////////////////////////////////////////////////////////////////////////////

bool FRigHierarchyRef::CreateHierarchy(const FName& RootName, const FRigHierarchyRef& SourceHierarchyRef)
{
	return CreateHierarchy(RootName, SourceHierarchyRef.Get());
}

bool FRigHierarchyRef::CreateHierarchy(const FName& RootName, const FRigHierarchy* SourceHierarchy)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Container)
	{
		FRigHierarchy* Found = Container->Find(Name);
		if (Found)
		{
			return false;
		}
		if (Name == NAME_None)
		{
			const FName NewName = (RootName != NAME_None)? RootName : FName(TEXT("NewName"));
			// find new unique name
			Name = UtilityHelpers::CreateUniqueName(NewName, [this](const FName& CurName) { return Container->Find(CurName) == nullptr; });
		}

		const FRigHierarchy* SourceToCopy = (SourceHierarchy) ? SourceHierarchy : &Container->BaseHierarchy;
		FRigHierarchy NewHierarchy;
		// whenever array reallocates, this will has to be fixed
		// default hierarchy is based on base
		if (RootName == NAME_None)
		{
			NewHierarchy = *SourceToCopy;
		}
		else
		{
			// add root, and all children
			int32 BoneIndex = SourceToCopy->GetIndex(RootName);
			if (BoneIndex != INDEX_NONE)
			{
				// add root first
				NewHierarchy.AddBone(RootName, NAME_None, SourceToCopy->Bones[BoneIndex].InitialTransform);

				// add all children
				TArray<int32> ChildIndices;
				SourceToCopy->GetChildren(RootName, ChildIndices, true);
				for (int32 ChildIndex = 0; ChildIndex < ChildIndices.Num(); ++ChildIndex)
				{
					const FRigBone& ChildBone = SourceToCopy->Bones[ChildIndices[ChildIndex]];
					NewHierarchy.AddBone(ChildBone.Name, ChildBone.ParentName, ChildBone.InitialTransform);
				}
			}
			else
			{
				// index not found, add warning
				// if somebody typed name, and didn't get to create, something went wrong
				return false;
			}
		}

		uint32& CurrentIndex = Container->MapContainer.Add(Name);
		CurrentIndex = Container->Hierarchies.Add(NewHierarchy);
		return (CurrentIndex != INDEX_NONE);
	}

	return false;
}

bool FRigHierarchyRef::MergeHierarchy(const FRigHierarchy* InSourceHierarchy)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// copy from source to this
	FRigHierarchy* MyHierarchy = Get();
	if (InSourceHierarchy && MyHierarchy)
	{
		for (int32 SourceBoneIndex = 0; SourceBoneIndex < InSourceHierarchy->GetNum(); ++SourceBoneIndex)
		{
			// first find same name, and apply to the this
			const FRigBone& SourceBone = InSourceHierarchy->Bones[SourceBoneIndex];
			const int32 TargetIndex = MyHierarchy->GetIndex(SourceBone.Name);
			if (TargetIndex != INDEX_NONE)
			{
				// copy source Bone
				// if parent changed, it will derive that data
				MyHierarchy->Bones[TargetIndex] = SourceBone;
			}
			else// if we don't find, that means it's new hierarchy
			{
				// parent should add first, so this should work
				MyHierarchy->AddBone(SourceBone.Name, SourceBone.ParentName, SourceBone.InitialTransform, SourceBone.LocalTransform, SourceBone.GlobalTransform);
			}
		}

		return true;
	}

	return false;
}

bool FRigHierarchyRef::MergeHierarchy(const FRigHierarchyRef& SourceHierarchyRef)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	return MergeHierarchy(SourceHierarchyRef.Get());
}


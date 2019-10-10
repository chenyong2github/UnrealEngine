// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigBoneHierarchy.h"
#include "ControlRig.h"
#include "HelperUtil.h"
#include "AnimationRuntime.h"

////////////////////////////////////////////////////////////////////////////////
// FRigBoneHierarchy
////////////////////////////////////////////////////////////////////////////////

FRigBoneHierarchy::FRigBoneHierarchy()
	:Container(nullptr)
{
}

FRigBoneHierarchy& FRigBoneHierarchy::operator= (const FRigBoneHierarchy &InOther)
{
#if WITH_EDITOR
	for (int32 Index = Num() - 1; Index >= 0; Index--)
	{
		FRigBone BoneToRemove = Bones[Index];
		OnBoneRemoved.Broadcast(Container, FRigElementKey(BoneToRemove.Name, ERigElementType::Bone));
	}
#endif

	Bones.Reset();
	Bones.Append(InOther.Bones);
	NameToIndexMapping.Reset();
	RefreshMapping();

#if WITH_EDITOR
	for (const FRigBone& BoneAdded : Bones)
	{
		OnBoneAdded.Broadcast(Container, FRigElementKey(BoneAdded.Name, ERigElementType::Bone));
	}
#endif

	return *this;
}

FName FRigBoneHierarchy::GetSafeNewName(const FName& InPotentialNewName) const
{
	FName Name = InPotentialNewName;
	int32 Suffix = 1;
	while(!IsNameAvailable(Name))
	{
		Name = *FString::Printf(TEXT("%s_%d"), *InPotentialNewName.ToString(), ++Suffix);
	}
	return Name;
}

FRigBone& FRigBoneHierarchy::Add(const FName& InNewName, const FName& InParentName, ERigBoneType InType, const FTransform& InInitTransform)
{
	FRigBone NewBone;
	NewBone.Name = GetSafeNewName(InNewName);
	NewBone.ParentIndex = GetIndex(InParentName);
	NewBone.ParentName = NewBone.ParentIndex == INDEX_NONE ? NAME_None : InParentName;
	NewBone.InitialTransform = InInitTransform;
	NewBone.GlobalTransform = InInitTransform;
	NewBone.Type = InType;
	RecalculateLocalTransform(NewBone);

	FName NewBoneName = NewBone.Name;
	Bones.Add(NewBone);
	RefreshMapping();

#if WITH_EDITOR
	OnBoneAdded.Broadcast(Container, NewBone.GetElementKey());
#endif

	int32 Index = GetIndex(NewBoneName);
	return Bones[Index];
}

FRigBone& FRigBoneHierarchy::Add(const FName& InNewName, const FName& InParentName, ERigBoneType InType, const FTransform& InInitTransform, const FTransform& InLocalTransform, const FTransform& InGlobalTransform)
{
	FRigBone& Bone = Add(InNewName, InParentName, InType, InInitTransform);
	Bone.LocalTransform = InLocalTransform;
	Bone.GlobalTransform = InGlobalTransform;
	return Bone;
}

bool FRigBoneHierarchy::Reparent(const FName& InName, const FName& InNewParentName)
{
	int32 Index = GetIndex(InName);

	if (Index != INDEX_NONE && InName != InNewParentName)
	{
		FRigBone& Bone= Bones[Index];

#if WITH_EDITOR
		FName OldParentName = Bone.ParentName;
#endif

		int32 ParentIndex = GetIndex(InNewParentName);
		if (Container != nullptr)
		{
			if (Container->IsParentedTo(ERigElementType::Bone, ParentIndex, ERigElementType::Bone, Index))
			{
				ParentIndex = INDEX_NONE;
			}
		}

		Bone.ParentIndex = ParentIndex;
		Bone.ParentName = Bone.ParentIndex == INDEX_NONE ? NAME_None : InNewParentName;
		RecalculateLocalTransform(Bone);

#if WITH_EDITOR
		FName NewParentName = Bone.ParentName;
#endif

		// we want to make sure parent is before the child
		RefreshMapping();

#if WITH_EDITOR
		if (OldParentName != NewParentName)
		{
			OnBoneReparented.Broadcast(Container, FRigElementKey(InName, RigElementType()), OldParentName, NewParentName);
		}
#endif
		return Bones[GetIndex(InName)].ParentName == InNewParentName;
	}

	return false;
}

FRigBone FRigBoneHierarchy::Remove(const FName& InNameToRemove)
{
	TArray<int32> Children;
#if WITH_EDITOR
	TArray<FName> RemovedChildBones;
#endif
	if (GetChildren(InNameToRemove, Children, true) > 0)
	{
		// sort by child index
		Children.Sort([](const int32& A, const int32& B) { return A < B; });

		// want to delete from end to the first 
		for (int32 ChildIndex = Children.Num() - 1; ChildIndex >= 0; --ChildIndex)
		{
#if WITH_EDITOR
			RemovedChildBones.Add(Bones[Children[ChildIndex]].Name);
#endif
			Bones.RemoveAt(Children[ChildIndex]);
		}
	}

	int32 IndexToDelete = GetIndex(InNameToRemove);
#if WITH_EDITOR
	Select(InNameToRemove, false);
#endif
	FRigBone RemovedBone = Bones[IndexToDelete];
	Bones.RemoveAt(IndexToDelete);

	RefreshMapping();

#if WITH_EDITOR
	for (const FName& RemovedChildBone : RemovedChildBones)
	{
		OnBoneRemoved.Broadcast(Container, FRigElementKey(RemovedChildBone, RigElementType()));
	}
	OnBoneRemoved.Broadcast(Container, RemovedBone.GetElementKey());
#endif

	return RemovedBone;
}

// list of names of children - this is not cheap, and is supposed to be used only for one time set up
int32 FRigBoneHierarchy::GetChildren(const FName& InName, TArray<int32>& OutChildren, bool bRecursively) const
{
	return GetChildren(GetIndex(InName), OutChildren, bRecursively);
}

int32 FRigBoneHierarchy::GetChildren(const int32 InIndex, TArray<int32>& OutChildren, bool bRecursively) const
{
	OutChildren.Reset();

	if (InIndex != INDEX_NONE)
	{
		GetChildrenRecursive(InIndex, OutChildren, bRecursively);
	}

	return OutChildren.Num();
}

FName FRigBoneHierarchy::GetName(int32 InIndex) const
{
	if (Bones.IsValidIndex(InIndex))
	{
		return Bones[InIndex].Name;
	}

	return NAME_None;
}

int32 FRigBoneHierarchy::GetIndexSlow(const FName& InName) const
{
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		if (Bones[Index].Name == InName)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void FRigBoneHierarchy::SetGlobalTransform(const FName& InName, const FTransform& InTransform, bool bPropagateTransform)
{
	SetGlobalTransform(GetIndex(InName), InTransform, bPropagateTransform);
}

void FRigBoneHierarchy::SetGlobalTransform(int32 InIndex, const FTransform& InTransform, bool bPropagateTransform)
{
	if (Bones.IsValidIndex(InIndex))
	{
		FRigBone& Bone = Bones[InIndex];
		Bone.GlobalTransform = InTransform;
		Bone.GlobalTransform.NormalizeRotation();
		RecalculateLocalTransform(Bone);

		if (bPropagateTransform)
		{
			PropagateTransform(InIndex);
		}
	}
}

FTransform FRigBoneHierarchy::GetGlobalTransform(const FName& InName) const
{
	return GetGlobalTransform(GetIndex(InName));
}

FTransform FRigBoneHierarchy::GetGlobalTransform(int32 InIndex) const
{
	if (Bones.IsValidIndex(InIndex))
	{
		return Bones[InIndex].GlobalTransform;
	}

	return FTransform::Identity;
}

void FRigBoneHierarchy::SetLocalTransform(const FName& InName, const FTransform& InTransform, bool bPropagateTransform)
{
	SetLocalTransform(GetIndex(InName), InTransform, bPropagateTransform);
}

void FRigBoneHierarchy::SetLocalTransform(int32 InIndex, const FTransform& InTransform, bool bPropagateTransform)
{
	if (Bones.IsValidIndex(InIndex))
	{
		FRigBone& Bone = Bones[InIndex];
		Bone.LocalTransform = InTransform;
		RecalculateGlobalTransform(Bone);

		if (bPropagateTransform)
		{
			PropagateTransform(InIndex);
		}
	}
}

FTransform FRigBoneHierarchy::GetLocalTransform(const FName& InName) const
{
	return GetLocalTransform(GetIndex(InName));
}

FTransform FRigBoneHierarchy::GetLocalTransform(int32 InIndex) const
{
	if (Bones.IsValidIndex(InIndex))
	{
		return Bones[InIndex].LocalTransform;
	}

	return FTransform::Identity;
}

void FRigBoneHierarchy::SetInitialTransform(const FName& InName, const FTransform& InTransform)
{
	SetInitialTransform(GetIndex(InName), InTransform);
}

void FRigBoneHierarchy::SetInitialTransform(int32 InIndex, const FTransform& InTransform)
{
	if (Bones.IsValidIndex(InIndex))
	{
		FRigBone& Bone = Bones[InIndex];
		Bone.InitialTransform = InTransform;
		Bone.InitialTransform.NormalizeRotation();
		RecalculateLocalTransform(Bone);
	}
}

FTransform FRigBoneHierarchy::GetInitialTransform(const FName& InName) const
{
	return GetInitialTransform(GetIndex(InName));
}

FTransform FRigBoneHierarchy::GetInitialTransform(int32 InIndex) const
{
	if (Bones.IsValidIndex(InIndex))
	{
		return Bones[InIndex].InitialTransform;
	}

	return FTransform::Identity;
}
void FRigBoneHierarchy::RecalculateLocalTransform(FRigBone& InOutBone)
{
	bool bHasParent = InOutBone.ParentIndex != INDEX_NONE;
	InOutBone.LocalTransform = (bHasParent) ? InOutBone.GlobalTransform.GetRelativeTransform(Bones[InOutBone.ParentIndex].GlobalTransform) : InOutBone.GlobalTransform;
}

void FRigBoneHierarchy::RecalculateGlobalTransform(FRigBone& InOutBone)
{
	bool bHasParent = InOutBone.ParentIndex != INDEX_NONE;
	InOutBone.GlobalTransform = (bHasParent) ? InOutBone.LocalTransform * Bones[InOutBone.ParentIndex].GlobalTransform : InOutBone.LocalTransform;
}

FName FRigBoneHierarchy::Rename(const FName& InOldName, const FName& InNewName)
{
	if (InOldName != InNewName)
	{
		const int32 Found = GetIndex(InOldName);
		if (Found != INDEX_NONE)
		{
			FName NewName = GetSafeNewName(InNewName);

#if WITH_EDITOR
			bool bWasSelected = IsSelected(InOldName);
			if(bWasSelected)
			{
				Select(InOldName, false);
			}
#endif

			Bones[Found].Name = NewName;

			// go through find all children and rename them
#if WITH_EDITOR
			TArray<FName> ReparentedBones;
#endif
			for (int32 Index = 0; Index < Bones.Num(); ++Index)
			{
				if (Bones[Index].ParentName == InOldName)
				{
					Bones[Index].ParentName = NewName;
					RecalculateLocalTransform(Bones[Index]);
#if WITH_EDITOR
					ReparentedBones.Add(Bones[Index].Name);
#endif
				}
			}

			RefreshMapping();

#if WITH_EDITOR
			OnBoneRenamed.Broadcast(Container, RigElementType(), InOldName, NewName);
			for (const FName& ReparentedBone : ReparentedBones)
			{
				OnBoneReparented.Broadcast(Container, FRigElementKey(ReparentedBone, RigElementType()), InOldName, NewName);
			}
			if (bWasSelected)
			{
				Select(NewName, true);
			}
#endif
			return NewName;
		}
	}

	return NAME_None;
}

void FRigBoneHierarchy::Sort()
{
	TArray<int32> RootArray;
	TMap<int32, TArray<int32>> HierarchyTree;

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
			RootArray.Add(Index);
		}
	}
	
	struct Local
	{
		static void VisitBone(int32 Index, const TMap<int32, TArray<int32>>& InHierarchyTree, TArray<int32>& SortedArray)
		{
			if (const TArray<int32>* ChildIndices = InHierarchyTree.Find(Index))
			{
				SortedArray.Append(*ChildIndices);
				for (int32 ChildIndex : *ChildIndices)
				{
					VisitBone(ChildIndex, InHierarchyTree, SortedArray);
				}
			}
		}
	};

	TArray<int32> SortedArray;
	for (int32 RootIndex : RootArray)
	{
		SortedArray.Add(RootIndex);
		Local::VisitBone(RootIndex, HierarchyTree, SortedArray);
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
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		Bones[Index].ParentIndex = GetIndexSlow(Bones[Index].ParentName);
		// parent index always should be less than this index, even if invalid
		check(Bones[Index].ParentIndex < Index);
	}
}

void FRigBoneHierarchy::RefreshParentNames()
{
	for (FRigBone& Bone : Bones)
	{
		Bone.ParentIndex = INDEX_NONE;
		if (Bone.ParentName != NAME_None)
		{
			Bone.ParentIndex = GetIndex(Bone.ParentName);
		}
	}
}

void FRigBoneHierarchy::RefreshMapping()
{
	RefreshParentNames();
	Sort();

	NameToIndexMapping.Empty();
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		Bones[Index].Index = Index;
		NameToIndexMapping.Add(Bones[Index].Name, Index);
	}
}

void FRigBoneHierarchy::Initialize()
{
	RefreshMapping();

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

void FRigBoneHierarchy::Reset()
{
	Bones.Reset();
}

void FRigBoneHierarchy::ResetTransforms()
{
	// initialize transform
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		Bones[Index].GlobalTransform = Bones[Index].InitialTransform;
		RecalculateLocalTransform(Bones[Index]);
	}
}

int32 FRigBoneHierarchy::GetChildrenRecursive(const int32 InIndex, TArray<int32>& OutChildren, bool bRecursively) const
{
	const int32 StartChildIndex = OutChildren.Num();

	// all children should be later than parent
	for (int32 ChildIndex = InIndex + 1; ChildIndex < Bones.Num(); ++ChildIndex)
	{
		if (Bones[ChildIndex].ParentIndex == InIndex)
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

void FRigBoneHierarchy::PropagateTransform(int32 InIndex)
{
	const TArray<int32> Dependents = Bones[InIndex].Dependents;
	for (int32 DependentIndex = 0; DependentIndex<Dependents.Num(); ++DependentIndex)
	{
		int32 Index = Dependents[DependentIndex];
		RecalculateGlobalTransform(Bones[Index]);
		PropagateTransform(Index);
	}
}

#if WITH_EDITOR

bool FRigBoneHierarchy::Select(const FName& InName, bool bSelect)
{
	if(GetIndex(InName) == INDEX_NONE)
	{
		return false;
	}

	if(bSelect == IsSelected(InName))
	{
		return false;
	}

	if(bSelect)
	{
		if (Container)
		{
			Container->SpaceHierarchy.ClearSelection();
			Container->ControlHierarchy.ClearSelection();
			Container->CurveContainer.ClearSelection();
		}

		Selection.Add(InName);
	}
	else
	{
		Selection.Remove(InName);
	}

	OnBoneSelected.Broadcast(Container, FRigElementKey(InName, RigElementType()), bSelect);

	return true;
}

bool FRigBoneHierarchy::ClearSelection()
{
	TArray<FName> TempSelection;
	TempSelection.Append(Selection);
	for(const FName& SelectedName : TempSelection)
	{
		Select(SelectedName, false);
	}
	return TempSelection.Num() > 0;
}

TArray<FName> FRigBoneHierarchy::CurrentSelection() const
{
	TArray<FName> TempSelection;
	TempSelection.Append(Selection);
	return TempSelection;
}

bool FRigBoneHierarchy::IsSelected(const FName& InName) const
{
	return Selection.Contains(InName);
}

TArray<FRigElementKey> FRigBoneHierarchy::ImportSkeleton(const FReferenceSkeleton& InSkeleton, const FName& InNameSpace, bool bReplaceExistingBones, bool bRemoveObsoleteBones, bool bSelectBones)
{
	TArray<FRigElementKey> AddedBones;
	TArray<FName> BoneNamesToSelect;
	TMap<FName, FName> BoneNameMap;

	const TArray<FMeshBoneInfo>& BoneInfos = InSkeleton.GetRefBoneInfo();
	const TArray<FTransform>& BonePoses = InSkeleton.GetRefBonePose();

	struct Local
	{
		static FName DetermineBoneName(const FName& InBoneName, const FName& InNameSpace)
		{
			if (InNameSpace == NAME_None || InBoneName == NAME_None)
			{
				return InBoneName;
			}
			return *FString::Printf(TEXT("%s_%s"), *InNameSpace.ToString(), *InBoneName.ToString());
		}
	};

	if (bReplaceExistingBones)
	{
		for (const FRigBone& ExistingBone : Bones)
		{
			BoneNameMap.Add(ExistingBone.Name, ExistingBone.Name);
		}

		for (int32 Index = 0; Index < BoneInfos.Num(); ++Index)
		{
			int32 ExistingBoneIndex = GetIndex(BoneInfos[Index].Name);
			FName DesiredBoneName = Local::DetermineBoneName(BoneInfos[Index].Name, InNameSpace);
			FName ParentName = (BoneInfos[Index].ParentIndex != INDEX_NONE) ? BoneInfos[BoneInfos[Index].ParentIndex].Name : NAME_None;
			ParentName = Local::DetermineBoneName(ParentName, InNameSpace);

			const FName* MappedParentNamePtr = BoneNameMap.Find(ParentName);
			if (MappedParentNamePtr)
			{
				ParentName = *MappedParentNamePtr;
			}

			if (ExistingBoneIndex != INDEX_NONE)
			{
				// check it's parent
				if (ParentName != NAME_None)
				{
					Bones[ExistingBoneIndex].ParentName = ParentName;
				}

				// reset it's initial transform
				SetInitialTransform(ExistingBoneIndex, FAnimationRuntime::GetComponentSpaceTransform(InSkeleton, BonePoses, Index));

				BoneNamesToSelect.Add(Bones[ExistingBoneIndex].Name);
			}
			else
			{
				FRigBone& AddedBone = Add(DesiredBoneName, ParentName, ERigBoneType::Imported, FAnimationRuntime::GetComponentSpaceTransform(InSkeleton, BonePoses, Index));
				BoneNameMap.Add(DesiredBoneName, AddedBone.Name);
				AddedBones.Add(AddedBone.GetElementKey());
				BoneNamesToSelect.Add(AddedBone.Name);
			}
		}

	}
	else // import all as new
	{
		for (int32 Index = 0; Index < BoneInfos.Num(); ++Index)
		{
			FName DesiredBoneName = Local::DetermineBoneName(BoneInfos[Index].Name, InNameSpace);
			FName ParentName = (BoneInfos[Index].ParentIndex != INDEX_NONE) ? BoneInfos[BoneInfos[Index].ParentIndex].Name : NAME_None;
			ParentName = Local::DetermineBoneName(ParentName, InNameSpace);

			const FName* MappedParentNamePtr = BoneNameMap.Find(ParentName);
			if (MappedParentNamePtr)
			{
				ParentName = *MappedParentNamePtr;
			}
			
			FRigBone& AddedBone = Add(DesiredBoneName, ParentName, ERigBoneType::Imported, FAnimationRuntime::GetComponentSpaceTransform(InSkeleton, BonePoses, Index));
			BoneNameMap.Add(DesiredBoneName, AddedBone.Name);
			AddedBones.Add(AddedBone.GetElementKey());
			BoneNamesToSelect.Add(AddedBone.Name);
		}
	}

	RefreshMapping();

	if (bReplaceExistingBones && bRemoveObsoleteBones)
	{
		TMap<FName, int32> BoneNameToIndexInSkeleton;
		for (const FMeshBoneInfo& BoneInfo : BoneInfos)
		{
			FName DesiredBoneName = Local::DetermineBoneName(BoneInfo.Name, InNameSpace);
			BoneNameToIndexInSkeleton.Add(DesiredBoneName, BoneNameToIndexInSkeleton.Num());
		}
		
		TArray<FName> BonesToDelete;
		for (int32 ExistingBoneIndex = 0; ExistingBoneIndex < Bones.Num(); ExistingBoneIndex++)
		{
			const FRigBone& Bone = Bones[ExistingBoneIndex];
			if (!BoneNameToIndexInSkeleton.Contains(Bone.Name))
			{
				if (Bone.Type == ERigBoneType::Imported)
				{
					BonesToDelete.Add(Bone.Name);
				}
			}
		}

		for (const FName& BoneToDelete : BonesToDelete)
		{
			const FRigBone& Bone = Bones[GetIndex(BoneToDelete)];
			TArray<FName> Dependents;
			for (int32 DependentIndex : Bone.Dependents)
			{
				if (Bones[DependentIndex].Type != ERigBoneType::Imported)
				{
					Dependents.Add(Bones[DependentIndex].Name);
				}
			}

			Algo::Reverse(Dependents);
			for (const FName& DependentName : Dependents)
			{
				Reparent(DependentName, NAME_None);
			}
		}

		for (const FName& BoneToDelete : BonesToDelete)
		{
			Remove(BoneToDelete);
			BoneNamesToSelect.Remove(BoneToDelete);
		}
	}

	if (bSelectBones)
	{
		ClearSelection();
		for (const FName& BoneNameToSelect : BoneNamesToSelect)
		{
			Select(BoneNameToSelect);
		}
	}

	Initialize();

	return AddedBones;
}


#endif
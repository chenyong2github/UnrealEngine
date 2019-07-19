// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigControlHierarchy.h"
#include "ControlRig.h"
#include "HelperUtil.h"

////////////////////////////////////////////////////////////////////////////////
// FRigControlHierarchy
////////////////////////////////////////////////////////////////////////////////

FRigControlHierarchy::FRigControlHierarchy()
	:Container(nullptr)
{
}

FRigControlHierarchy& FRigControlHierarchy::operator= (const FRigControlHierarchy &InOther)
{
#if WITH_EDITOR
	for (int32 Index = Num() - 1; Index >= 0; Index--)
	{
		FRigControl ControlToRemove = Controls[Index];
		OnControlRemoved.Broadcast(Container, RigElementType(), ControlToRemove.Name);
	}
#endif

	Controls.Reset();
	Controls.Append(InOther.Controls);
	NameToIndexMapping.Reset();
	RefreshMapping();

#if WITH_EDITOR
	for (const FRigControl& ControlAdded : Controls)
	{
		OnControlAdded.Broadcast(Container, RigElementType(), ControlAdded.Name);
	}
#endif

	return *this;
}

FName FRigControlHierarchy::GetSafeNewName(const FName& InPotentialNewName) const
{
	FName Name = InPotentialNewName;
	int32 Suffix = 1;
	while(!IsNameAvailable(Name))
	{
		Name = *FString::Printf(TEXT("%s_%d"), *InPotentialNewName.ToString(), ++Suffix);
	}
	return Name;
}

FRigControl& FRigControlHierarchy::Add(const FName& InNewName, ERigControlType InControlType, const FName& InParentName, const FName& InSpaceName, const FTransform& InTransform)
{
	FRigControl NewControl;
	NewControl.Name = GetSafeNewName(InNewName);
	NewControl.ControlType = InControlType;
	NewControl.ParentIndex = GetIndex(InParentName);
	NewControl.ParentName = NewControl.ParentIndex == INDEX_NONE ? NAME_None : InParentName;
	NewControl.SpaceIndex = INDEX_NONE;
	NewControl.SpaceName = NAME_None;
	NewControl.InitialTransform = InTransform;
	NewControl.LocalTransform = InTransform;

	FName NewControlName = NewControl.Name;
	Controls.Add(NewControl);
	RefreshMapping();

#if WITH_EDITOR
	OnControlAdded.Broadcast(Container, RigElementType(), NewControlName);
#endif

	SetSpace(NewControlName, InSpaceName);

	int32 Index = GetIndex(NewControlName);
	return Controls[Index];
}

bool FRigControlHierarchy::Reparent(const FName& InName, const FName& InNewParentName)
{
	int32 Index = GetIndex(InName);
	// can't parent to itself
	if (Index != INDEX_NONE && InName != InNewParentName)
	{
		FRigControl& Control = Controls[Index];

#if WITH_EDITOR
		FName OldParentName = Control.ParentName;
#endif

		struct Local
		{
			static bool IsParentedTo(int32 Child, int32 Parent, const TArray<FRigControl>& Controls)
			{
				if (Parent == INDEX_NONE || Child == INDEX_NONE)
				{
					return false;
				}

				if (Child == Parent)
				{
					return true;
				}

				if (Controls[Child].ParentIndex == Parent)
				{
					return true;
				}

				return IsParentedTo(Controls[Child].ParentIndex, Parent, Controls);
			}
		};

		int32 ParentIndex = GetIndex(InNewParentName);
		if (Local::IsParentedTo(ParentIndex, Index, Controls))
		{
			ParentIndex = INDEX_NONE;
		}

		Control.ParentIndex = ParentIndex;
		Control.ParentName = Control.ParentIndex == INDEX_NONE ? NAME_None : InNewParentName;

#if WITH_EDITOR
		FName NewParentName = Control.ParentName;
#endif

		RefreshMapping();

#if WITH_EDITOR
		if (OldParentName != NewParentName)
		{
			OnControlReparented.Broadcast(Container, RigElementType(), InName, OldParentName, NewParentName);
		}
#endif
		return Controls[GetIndex(InName)].ParentName == InNewParentName;
	}
	return false;
}

void FRigControlHierarchy::SetSpace(const FName& InName, const FName& InNewSpaceName)
{
	int32 Index = GetIndex(InName);
	if (Index != INDEX_NONE)
	{
		int32 SpaceIndex = GetSpaceIndex(InNewSpaceName);

		if (SpaceIndex != INDEX_NONE)
		{
			if (Container != nullptr)
			{
				if (Container->IsParentedTo(ERigElementType::Space, SpaceIndex, ERigElementType::Control, Index))
				{
					SpaceIndex = INDEX_NONE;
				}
			}
		}

		Controls[Index].SpaceIndex = SpaceIndex;
		Controls[Index].SpaceName = Controls[Index].SpaceIndex == INDEX_NONE ? NAME_None : InNewSpaceName;

#if WITH_EDITOR
		OnControlReparented.Broadcast(Container, RigElementType(), InName, Controls[Index].ParentName, Controls[Index].ParentName);
#endif
	}
}

FRigControl FRigControlHierarchy::Remove(const FName& InNameToRemove)
{
	TArray<int32> Children;
#if WITH_EDITOR
	TArray<FName> RemovedChildControls;
#endif
	if (GetChildren(InNameToRemove, Children, true) > 0)
	{
		// sort by child index
		Children.Sort([](const int32& A, const int32& B) { return A < B; });

		// want to delete from end to the first 
		for (int32 ChildIndex = Children.Num() - 1; ChildIndex >= 0; --ChildIndex)
		{
#if WITH_EDITOR
			RemovedChildControls.Add(Controls[Children[ChildIndex]].Name);
#endif
			Controls.RemoveAt(Children[ChildIndex]);
		}
	}

	int32 IndexToDelete = GetIndex(InNameToRemove);
#if WITH_EDITOR
	Select(InNameToRemove, false);
#endif
	FRigControl RemovedControl = Controls[IndexToDelete];
	Controls.RemoveAt(IndexToDelete);

	RefreshMapping();

#if WITH_EDITOR
	for (const FName& RemovedChildControl : RemovedChildControls)
	{
		OnControlRemoved.Broadcast(Container, RigElementType(), RemovedChildControl);
	}
	OnControlRemoved.Broadcast(Container, RigElementType(), RemovedControl.Name);
#endif

	return RemovedControl;
}

// list of names of children - this is not cheap, and is supposed to be used only for one time set up
int32 FRigControlHierarchy::GetChildren(const FName& InName, TArray<int32>& OutChildren, bool bRecursively) const
{
	return GetChildren(GetIndex(InName), OutChildren, bRecursively);
}

int32 FRigControlHierarchy::GetChildren(const int32 InIndex, TArray<int32>& OutChildren, bool bRecursively) const
{
	OutChildren.Reset();

	if (InIndex != INDEX_NONE)
	{
		GetChildrenRecursive(InIndex, OutChildren, bRecursively);
	}

	return OutChildren.Num();
}

FName FRigControlHierarchy::GetName(int32 InIndex) const
{
	if (Controls.IsValidIndex(InIndex))
	{
		return Controls[InIndex].Name;
	}

	return NAME_None;
}

int32 FRigControlHierarchy::GetIndexSlow(const FName& InName) const
{
	for (int32 Index = 0; Index < Controls.Num(); ++Index)
	{
		if (Controls[Index].Name == InName)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void FRigControlHierarchy::SetGlobalTransform(const FName& InName, const FTransform& InTransform)
{
	SetGlobalTransform(GetIndex(InName), InTransform);
}

void FRigControlHierarchy::SetGlobalTransform(int32 InIndex, const FTransform& InTransform)
{
	if (Container == nullptr)
	{
		SetLocalTransform(InIndex, InTransform);
		return;
	}

	if (Controls.IsValidIndex(InIndex))
	{
		const FRigControl& Control = Controls[InIndex];
		if (Control.SpaceIndex == INDEX_NONE)
		{
			SetLocalTransform(InIndex, InTransform);
			return;
		}
		else
		{
			FTransform ParentTransform = Container->GetGlobalTransform(ERigElementType::Space, Control.SpaceIndex);
			SetLocalTransform(InIndex, InTransform.GetRelativeTransform(ParentTransform));
		}
	}
}

FTransform FRigControlHierarchy::GetGlobalTransform(const FName& InName) const
{
	return GetGlobalTransform(GetIndex(InName));
}

FTransform FRigControlHierarchy::GetGlobalTransform(int32 InIndex) const
{
	if (Container == nullptr)
	{
		return GetLocalTransform(InIndex);
	}

	if (Controls.IsValidIndex(InIndex))
	{
		const FRigControl& Control = Controls[InIndex];
		if (Control.SpaceIndex == INDEX_NONE)
		{
			return Control.LocalTransform;
		}
		else
		{
			FTransform ParentTransform = Container->GetGlobalTransform(ERigElementType::Space, Control.SpaceIndex);
			return Control.LocalTransform * ParentTransform;
		}
	}

	return FTransform::Identity;
}

void FRigControlHierarchy::SetLocalTransform(const FName& InName, const FTransform& InTransform)
{
	SetLocalTransform(GetIndex(InName), InTransform);
}

void FRigControlHierarchy::SetLocalTransform(int32 InIndex, const FTransform& InTransform)
{
	if (Controls.IsValidIndex(InIndex))
	{
		FRigControl& Control = Controls[InIndex];
		Control.LocalTransform = InTransform;
		Control.LocalTransform.NormalizeRotation();
	}
}

FTransform FRigControlHierarchy::GetLocalTransform(const FName& InName) const
{
	return GetLocalTransform(GetIndex(InName));
}

FTransform FRigControlHierarchy::GetLocalTransform(int32 InIndex) const
{
	if (Controls.IsValidIndex(InIndex))
	{
		return Controls[InIndex].LocalTransform;
	}

	return FTransform::Identity;
}

void FRigControlHierarchy::SetInitialTransform(const FName& InName, const FTransform& InTransform)
{
	SetInitialTransform(GetIndex(InName), InTransform);
}

void FRigControlHierarchy::SetInitialTransform(int32 InIndex, const FTransform& InTransform)
{
	if (Controls.IsValidIndex(InIndex))
	{
		FRigControl& Control = Controls[InIndex];
		Control.InitialTransform = InTransform;
		Control.InitialTransform.NormalizeRotation();
	}
}

FTransform FRigControlHierarchy::GetInitialTransform(const FName& InName) const
{
	return GetInitialTransform(GetIndex(InName));
}

FTransform FRigControlHierarchy::GetInitialTransform(int32 InIndex) const
{
	if (Controls.IsValidIndex(InIndex))
	{
		return Controls[InIndex].InitialTransform;
	}

	return FTransform::Identity;
}

FName FRigControlHierarchy::Rename(const FName& InOldName, const FName& InNewName)
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

			Controls[Found].Name = NewName;

			// go through find all children and rename them
#if WITH_EDITOR
			TArray<FName> ReparentedControls;
#endif
			for (int32 Index = 0; Index < Controls.Num(); ++Index)
			{
				if (Controls[Index].ParentName == InOldName)
				{
					Controls[Index].ParentName = NewName;
#if WITH_EDITOR
					ReparentedControls.Add(Controls[Index].Name);
#endif
				}
			}

			RefreshMapping();

#if WITH_EDITOR
			OnControlRenamed.Broadcast(Container, RigElementType(), InOldName, NewName);
			for (const FName& ReparentedControl : ReparentedControls)
			{
				OnControlReparented.Broadcast(Container, RigElementType(), ReparentedControl, InOldName, NewName);
			}
			if(bWasSelected)
			{
				Select(NewName, true);
			}
#endif
			return NewName;
		}
	}

	return NAME_None;
}

void FRigControlHierarchy::Sort()
{
	TMap<int32, TArray<int32>> HierarchyTree;

	TArray<int32> SortedArray;

	// first figure out children and find roots
	for (int32 Index = 0; Index < Controls.Num(); ++Index)
	{
		int32 ParentIndex = GetIndexSlow(Controls[Index].ParentName);
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

	check(SortedArray.Num() == Controls.Num());

	// create new list with sorted
	TArray<FRigControl> NewSortedList;
	NewSortedList.AddDefaulted(Controls.Num());
	for (int32 NewIndex = 0; NewIndex < SortedArray.Num(); ++NewIndex)
	{
		NewSortedList[NewIndex] = Controls[SortedArray[NewIndex]];
	}

	Controls = MoveTemp(NewSortedList);

	// now fix up parent Index
	for (int32 Index = 0; Index < Controls.Num(); ++Index)
	{
		Controls[Index].ParentIndex = GetIndexSlow(Controls[Index].ParentName);
		// parent index always should be less than this index, even if invalid
		check(Controls[Index].ParentIndex < Index);
	}
}

void FRigControlHierarchy::RefreshMapping()
{
	Sort();

	NameToIndexMapping.Empty();
	for (int32 Index = 0; Index < Controls.Num(); ++Index)
	{
		Controls[Index].Index = Index;
		NameToIndexMapping.Add(Controls[Index].Name, Index);
	}
}

void FRigControlHierarchy::Initialize()
{
	RefreshMapping();

	// update parent index
	for (int32 Index = 0; Index < Controls.Num(); ++Index)
	{
		Controls[Index].ParentIndex = GetIndex(Controls[Index].ParentName);
		// todo
		//Controls[Index].SpaceIndex = GetIndex(Controls[Index].ParentName);
	}

	// initialize transform
	for (int32 Index = 0; Index < Controls.Num(); ++Index)
	{
		Controls[Index].LocalTransform = Controls[Index].InitialTransform;

		// update children
		GetChildren(Index, Controls[Index].Dependents, false);
	}
}

void FRigControlHierarchy::Reset()
{
	Controls.Reset();
}

void FRigControlHierarchy::ResetTransforms()
{
	// initialize transform
	for (int32 Index = 0; Index < Controls.Num(); ++Index)
	{
		Controls[Index].LocalTransform = Controls[Index].InitialTransform;
	}
}

int32 FRigControlHierarchy::GetChildrenRecursive(const int32 InIndex, TArray<int32>& OutChildren, bool bRecursively) const
{
	const int32 StartChildIndex = OutChildren.Num();

	// all children should be later than parent
	for (int32 ChildIndex = InIndex + 1; ChildIndex < Controls.Num(); ++ChildIndex)
	{
		if (Controls[ChildIndex].ParentIndex == InIndex)
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

int32 FRigControlHierarchy::GetSpaceIndex(const FName& InName) const
{
	if (Container == nullptr || InName == NAME_None)
	{
		return INDEX_NONE;
	}
	return Container->GetIndex(ERigElementType::Space, InName);
}

#if WITH_EDITOR

bool FRigControlHierarchy::Select(const FName& InName, bool bSelect)
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
			Container->BoneHierarchy.ClearSelection();
			Container->SpaceHierarchy.ClearSelection();
			Container->CurveContainer.ClearSelection();
		}

		Selection.Add(InName);
	}
	else
	{
		Selection.Remove(InName);
	}

	OnControlSelected.Broadcast(Container, RigElementType(), InName, bSelect);

	return true;
}

bool FRigControlHierarchy::ClearSelection()
{
	TArray<FName> TempSelection;
	TempSelection.Append(Selection);
	for(const FName& SelectedName : TempSelection)
	{
		Select(SelectedName, false);
	}
	return TempSelection.Num() > 0;
}

TArray<FName> FRigControlHierarchy::CurrentSelection() const
{
	TArray<FName> TempSelection;
	TempSelection.Append(Selection);
	return TempSelection;
}

bool FRigControlHierarchy::IsSelected(const FName& InName) const
{
	return Selection.Contains(InName);
}

void FRigControlHierarchy::HandleOnElementRemoved(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InName)
{
	if (Container == nullptr)
	{
		return;
	}

	switch (InElementType)
	{
	case ERigElementType::Space:
	{
		for (FRigControl& Control : Controls)
		{
			if (Control.SpaceName == InName)
			{
				Control.SpaceIndex = INDEX_NONE;
				Control.SpaceName = NAME_None;
#if WITH_EDITOR
				OnControlReparented.Broadcast(Container, RigElementType(), Control.Name, Control.ParentName, Control.ParentName);
#endif
			}
		}
		break;
	}
	case ERigElementType::Bone:
	case ERigElementType::Control:
	case ERigElementType::Curve:
	{
		break;
	}
	}
}

void FRigControlHierarchy::HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName)
{
	if (Container == nullptr)
	{
		return;
	}

	switch (InElementType)
	{
		case ERigElementType::Space:
		{
			for (FRigControl& Control : Controls)
			{
				if (Control.SpaceName == InOldName)
				{
					Control.SpaceIndex = Container->SpaceHierarchy.GetIndex(InNewName);
					Control.SpaceName = Control.SpaceIndex == INDEX_NONE ? NAME_None : InNewName;
#if WITH_EDITOR
					OnControlReparented.Broadcast(Container, RigElementType(), Control.Name, Control.ParentName, Control.ParentName);
#endif
				}
			}
			break;
		}
		case ERigElementType::Bone:
		case ERigElementType::Control:
		case ERigElementType::Curve:
		{
			break;
		}
	}
}
#endif
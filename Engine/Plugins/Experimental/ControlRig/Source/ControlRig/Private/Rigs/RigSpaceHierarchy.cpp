// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigSpaceHierarchy.h"
#include "ControlRig.h"
#include "HelperUtil.h"

////////////////////////////////////////////////////////////////////////////////
// FRigSpaceHierarchy
////////////////////////////////////////////////////////////////////////////////

FRigSpaceHierarchy::FRigSpaceHierarchy()
	:Container(nullptr)
{
}

FRigSpaceHierarchy& FRigSpaceHierarchy::operator= (const FRigSpaceHierarchy &InOther)
{
#if WITH_EDITOR
	for (int32 Index = Num() - 1; Index >= 0; Index--)
	{
		FRigSpace SpaceToRemove = Spaces[Index];
		OnSpaceRemoved.Broadcast(Container, FRigElementKey(SpaceToRemove.Name, ERigElementType::Space));
	}
#endif

	Spaces.Reset();
	Spaces.Append(InOther.Spaces);
	NameToIndexMapping.Reset();
	RefreshMapping();

#if WITH_EDITOR
	for (const FRigSpace& SpaceAdded : Spaces)
	{
		OnSpaceAdded.Broadcast(Container, FRigElementKey(SpaceAdded.Name, ERigElementType::Space));
	}
#endif

	return *this;
}

FName FRigSpaceHierarchy::GetSafeNewName(const FName& InPotentialNewName) const
{
	FName Name = InPotentialNewName;
	int32 Suffix = 1;
	while(!IsNameAvailable(Name))
	{
		Name = *FString::Printf(TEXT("%s_%d"), *InPotentialNewName.ToString(), ++Suffix);
	}
	return Name;
}

FRigSpace& FRigSpaceHierarchy::Add(const FName& InNewName, ERigSpaceType InSpaceType, const FName& InParentName, const FTransform& InTransform)
{
	FRigSpace NewSpace;
	NewSpace.Name = GetSafeNewName(InNewName);
	NewSpace.ParentIndex = GetParentIndex(InSpaceType, InParentName);
	NewSpace.SpaceType = NewSpace.ParentIndex == INDEX_NONE ? ERigSpaceType::Global : InSpaceType;
	NewSpace.ParentName = NewSpace.ParentIndex == INDEX_NONE ? NAME_None : InParentName;
	NewSpace.InitialTransform = InTransform;
	NewSpace.LocalTransform = InTransform;
	FName NewSpaceName = NewSpace.Name;
	Spaces.Add(NewSpace);
	RefreshMapping();

#if WITH_EDITOR
	OnSpaceAdded.Broadcast(Container, NewSpace.GetElementKey());
#endif

	int32 Index = GetIndex(NewSpaceName);
	return Spaces[Index];
}

bool FRigSpaceHierarchy::Reparent(const FName& InName, ERigSpaceType InSpaceType, const FName& InNewParentName)
{
	int32 Index = GetIndex(InName);
	if (Index != INDEX_NONE)
	{
		FRigSpace& Space = Spaces[Index];

#if WITH_EDITOR
		FName OldParentName = Space.ParentName;
#endif

		int32 ParentIndex = GetParentIndex(InSpaceType, InNewParentName);
		if (ParentIndex != INDEX_NONE)
		{
			if (Container != nullptr)
			{
				switch (InSpaceType)
				{
					case ERigSpaceType::Global:
					{
						ParentIndex = INDEX_NONE;
						break;
					}
					case ERigSpaceType::Bone:
					{
						break;
					}
					case ERigSpaceType::Space:
					{
						if (Container->IsParentedTo(ERigElementType::Space, ParentIndex, ERigElementType::Space, Index))
						{
							ParentIndex = INDEX_NONE;
						}
						break;
					}
					case ERigSpaceType::Control:
					{
						if (Container->IsParentedTo(ERigElementType::Control, ParentIndex, ERigElementType::Space, Index))
						{
							ParentIndex = INDEX_NONE;
						}
						break;
					}
				}
			}
		}

		Space.ParentIndex = ParentIndex;
		Space.SpaceType = Space.ParentIndex == INDEX_NONE ? ERigSpaceType::Global : InSpaceType;
		Space.ParentName = Space.ParentIndex == INDEX_NONE ? NAME_None : InNewParentName;

#if WITH_EDITOR
		FName NewParentName = Space.ParentName;
#endif

		RefreshMapping();

#if WITH_EDITOR
		if (OldParentName != NewParentName)
		{
			OnSpaceReparented.Broadcast(Container, Space.GetElementKey(), OldParentName, NewParentName);
		}
#endif		
		return Spaces[GetIndex(InName)].ParentName == InNewParentName;
	}
	return false;
}

FRigSpace FRigSpaceHierarchy::Remove(const FName& InNameToRemove)
{
	int32 IndexToDelete = GetIndex(InNameToRemove);
#if WITH_EDITOR
	Select(InNameToRemove, false);
#endif
	FRigSpace RemovedSpace = Spaces[IndexToDelete];
	Spaces.RemoveAt(IndexToDelete);

#if WITH_EDITOR
	TArray<FName> SpacesReparented;
#endif
	for (FRigSpace& ChildSpace : Spaces)
	{
		if (ChildSpace.SpaceType == ERigSpaceType::Space && ChildSpace.ParentName == InNameToRemove)
		{
			ChildSpace.SpaceType = ERigSpaceType::Global;
			ChildSpace.ParentIndex = INDEX_NONE;
			ChildSpace.ParentName = NAME_None;
#if WITH_EDITOR
			SpacesReparented.Add(ChildSpace.Name);
#endif
		}
	}

	RefreshMapping();

#if WITH_EDITOR
	for (const FName& ReparentedSpace : SpacesReparented)
	{
		OnSpaceReparented.Broadcast(Container, FRigElementKey(ReparentedSpace, RigElementType()), InNameToRemove, NAME_None);
	}
	OnSpaceRemoved.Broadcast(Container, RemovedSpace.GetElementKey());
#endif

	return RemovedSpace;
}

FName FRigSpaceHierarchy::GetName(int32 InIndex) const
{
	if (Spaces.IsValidIndex(InIndex))
	{
		return Spaces[InIndex].Name;
	}

	return NAME_None;
}

int32 FRigSpaceHierarchy::GetIndexSlow(const FName& InName) const
{
	for (int32 Index = 0; Index < Spaces.Num(); ++Index)
	{
		if (Spaces[Index].Name == InName)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void FRigSpaceHierarchy::SetGlobalTransform(const FName& InName, const FTransform& InTransform)
{
	SetGlobalTransform(GetIndex(InName), InTransform);
}

void FRigSpaceHierarchy::SetGlobalTransform(int32 InIndex, const FTransform& InTransform)
{
	if (Container == nullptr)
	{
		SetLocalTransform(InIndex, InTransform);
		return;
	}

	if (Spaces.IsValidIndex(InIndex))
	{
		FRigSpace& Space = Spaces[InIndex];
		if (Space.SpaceType == ERigSpaceType::Global)
		{
			SetLocalTransform(InIndex, InTransform);
			return;
		}

		FTransform ParentTransform = Container->GetGlobalTransform(Space.GetParentElementKey());
		SetLocalTransform(InIndex, InTransform.GetRelativeTransform(ParentTransform));
	}
}

FTransform FRigSpaceHierarchy::GetGlobalTransform(const FName& InName) const
{
	return GetGlobalTransform(GetIndex(InName));
}

FTransform FRigSpaceHierarchy::GetGlobalTransform(int32 InIndex) const
{
	if(Container == nullptr)
	{
		return GetLocalTransform(InIndex);
	}

	if (Spaces.IsValidIndex(InIndex))
	{
		const FRigSpace& Space = Spaces[InIndex];
		switch (Space.SpaceType)
		{
			case ERigSpaceType::Global:
			{
				return Space.LocalTransform;
			}
			case ERigSpaceType::Bone:
			{
				FTransform Transform = Space.LocalTransform * Container->GetGlobalTransform(ERigElementType::Bone, Space.ParentIndex);
				Transform.NormalizeRotation();
				return Transform;
			}
			case ERigSpaceType::Space:
			{
				FTransform Transform = Space.LocalTransform * Container->GetGlobalTransform(ERigElementType::Space, Space.ParentIndex);
				Transform.NormalizeRotation();
				return Transform;
			}
			case ERigSpaceType::Control:
			{
				FTransform Transform = Space.LocalTransform * Container->GetGlobalTransform(ERigElementType::Control, Space.ParentIndex);
				Transform.NormalizeRotation();
				return Transform;
			}
		}
	}

	return FTransform::Identity;
}

void FRigSpaceHierarchy::SetLocalTransform(const FName& InName, const FTransform& InTransform)
{
	SetLocalTransform(GetIndex(InName), InTransform);
}

void FRigSpaceHierarchy::SetLocalTransform(int32 InIndex, const FTransform& InTransform)
{
	if (Spaces.IsValidIndex(InIndex))
	{
		FRigSpace& Space = Spaces[InIndex];
		Space.LocalTransform = InTransform;
		Space.LocalTransform.NormalizeRotation();
	}
}

FTransform FRigSpaceHierarchy::GetLocalTransform(const FName& InName) const
{
	return GetLocalTransform(GetIndex(InName));
}

FTransform FRigSpaceHierarchy::GetLocalTransform(int32 InIndex) const
{
	if (Spaces.IsValidIndex(InIndex))
	{
		return Spaces[InIndex].LocalTransform;
	}

	return FTransform::Identity;
}

void FRigSpaceHierarchy::SetInitialTransform(const FName& InName, const FTransform& InTransform)
{
	SetInitialTransform(GetIndex(InName), InTransform);
}

void FRigSpaceHierarchy::SetInitialTransform(int32 InIndex, const FTransform& InTransform)
{
	if (Spaces.IsValidIndex(InIndex))
	{
		FRigSpace& Space = Spaces[InIndex];
		Space.InitialTransform = InTransform;
		Space.InitialTransform.NormalizeRotation();
	}
}

FTransform FRigSpaceHierarchy::GetInitialTransform(const FName& InName) const
{
	return GetInitialTransform(GetIndex(InName));
}

FTransform FRigSpaceHierarchy::GetInitialTransform(int32 InIndex) const
{
	if (Spaces.IsValidIndex(InIndex))
	{
		return Spaces[InIndex].InitialTransform;
	}

	return FTransform::Identity;
}

void FRigSpaceHierarchy::SetInitialGlobalTransform(const FName& InName, const FTransform& GlobalTransform)
{
	SetInitialGlobalTransform(GetIndex(InName), GlobalTransform);
}

// wip - should support all types that can provide space transform data (pos/rotation)
void FRigSpaceHierarchy::SetInitialGlobalTransform(int32 InIndex, const FTransform& GlobalTransform)
{
	if (Spaces.IsValidIndex(InIndex))
	{
		FRigSpace& Space = Spaces[InIndex];
		FTransform ParentTransform = FTransform::Identity;
		if (Container)
		{
			ParentTransform = Container->GetInitialGlobalTransform(Space.GetParentElementKey());
		}
		Space.InitialTransform = GlobalTransform.GetRelativeTransform(ParentTransform);
	}
}

FTransform FRigSpaceHierarchy::GetInitialGlobalTransform(const FName& InName) const
{
	return GetInitialGlobalTransform(GetIndex(InName));
}

FTransform FRigSpaceHierarchy::GetInitialGlobalTransform(int32 InIndex) const
{
	// @todo: Templatize
	if (Spaces.IsValidIndex(InIndex))
	{
		const FRigSpace& Space = Spaces[InIndex];
		FTransform ParentTransform = FTransform::Identity;
		if (Container)
		{
			ParentTransform = Container->GetInitialGlobalTransform(Space.GetParentElementKey());
		}
		FTransform Transform = Space.InitialTransform * ParentTransform;
		Transform.NormalizeRotation();
		return Transform;
	}
	return FTransform::Identity;
}

FName FRigSpaceHierarchy::Rename(const FName& InOldName, const FName& InNewName)
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

			Spaces[Found].Name = NewName;

			// go through find all children and rename them
			for (int32 Index = 0; Index < Spaces.Num(); ++Index)
			{
				if (Spaces[Index].SpaceType == ERigSpaceType::Space && Spaces[Index].ParentName == InOldName)
				{
					Spaces[Index].ParentName = NewName;
				}
			}

			RefreshMapping();

#if WITH_EDITOR
			OnSpaceRenamed.Broadcast(Container, RigElementType(), InOldName, NewName);
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

void FRigSpaceHierarchy::RefreshMapping()
{
	NameToIndexMapping.Empty();
	for (int32 Index = 0; Index < Spaces.Num(); ++Index)
	{
		Spaces[Index].Index = Index;
		NameToIndexMapping.Add(Spaces[Index].Name, Index);
	}
}

void FRigSpaceHierarchy::Initialize()
{
	RefreshMapping();

	// initialize transform
	for (int32 Index = 0; Index < Spaces.Num(); ++Index)
	{
		if (Container)
		{
			Spaces[Index].ParentIndex = Container->GetIndex(Spaces[Index].GetParentElementKey());
		}
		Spaces[Index].LocalTransform = Spaces[Index].InitialTransform;
	}
}

void FRigSpaceHierarchy::Reset()
{
	Spaces.Reset();
}

void FRigSpaceHierarchy::ResetTransforms()
{
	// initialize transform
	for (int32 Index = 0; Index < Spaces.Num(); ++Index)
	{
		Spaces[Index].LocalTransform = Spaces[Index].InitialTransform;
	}
}

int32 FRigSpaceHierarchy::GetParentIndex(ERigSpaceType InSpaceType, const FName& InName) const
{
	if (Container != nullptr)
	{
		switch (InSpaceType)
		{
			case ERigSpaceType::Global:
			{
				return INDEX_NONE;
			}
			case ERigSpaceType::Bone:
			{
				return Container->GetIndex(FRigElementKey(InName, ERigElementType::Bone));
			}
			case ERigSpaceType::Space:
			{
				return Container->GetIndex(FRigElementKey(InName, ERigElementType::Space));
			}
			case ERigSpaceType::Control:
			{
				return Container->GetIndex(FRigElementKey(InName, ERigElementType::Control));
			}
		}
	}

	return INDEX_NONE;
}

#if WITH_EDITOR

bool FRigSpaceHierarchy::Select(const FName& InName, bool bSelect)
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
			Container->ControlHierarchy.ClearSelection();
			Container->CurveContainer.ClearSelection();
		}

		Selection.Add(InName);
	}
	else
	{
		Selection.Remove(InName);
	}

	OnSpaceSelected.Broadcast(Container, FRigElementKey(InName, RigElementType()), bSelect);

	return true;
}

bool FRigSpaceHierarchy::ClearSelection()
{
	TArray<FName> TempSelection;
	TempSelection.Append(Selection);
	for(const FName& SelectedName : TempSelection)
	{
		Select(SelectedName, false);
	}
	return TempSelection.Num() > 0;
}

TArray<FName> FRigSpaceHierarchy::CurrentSelection() const
{
	TArray<FName> TempSelection;
	TempSelection.Append(Selection);
	return TempSelection;
}

bool FRigSpaceHierarchy::IsSelected(const FName& InName) const
{
	return Selection.Contains(InName);
}

void FRigSpaceHierarchy::HandleOnElementRemoved(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey)
{
	if (Container == nullptr)
	{
		return;
	}

	switch (InKey.Type)
	{
		case ERigElementType::Bone:
		{
			for (FRigSpace& Space : Spaces)
			{
				if (Space.SpaceType == ERigSpaceType::Bone && Space.ParentName == InKey.Name)
				{
					Space.ParentIndex = Container->BoneHierarchy.GetIndex(InKey.Name);
					Space.ParentName = Space.ParentIndex == INDEX_NONE ? NAME_None : InKey.Name;
					Space.SpaceType = Space.ParentIndex == INDEX_NONE ? ERigSpaceType::Global : Space.SpaceType;
#if WITH_EDITOR
					OnSpaceReparented.Broadcast(Container, Space.GetElementKey(), InKey.Name, Space.ParentName);
#endif
				}
			}
			break;
		}
		case ERigElementType::Control:
		{
			for (FRigSpace& Space : Spaces)
			{
				if (Space.SpaceType == ERigSpaceType::Control && Space.ParentName == InKey.Name)
				{
					Space.ParentIndex = Container->ControlHierarchy.GetIndex(InKey.Name);
					Space.ParentName = Space.ParentIndex == INDEX_NONE ? NAME_None : InKey.Name;
					Space.SpaceType = Space.ParentIndex == INDEX_NONE ? ERigSpaceType::Global : Space.SpaceType;
#if WITH_EDITOR
					OnSpaceReparented.Broadcast(Container, Space.GetElementKey(), InKey.Name, Space.ParentName);
#endif
				}
			}
			break;
		}
		case ERigElementType::Space:
		case ERigElementType::Curve:
		{
			break;
		}
	}
}

void FRigSpaceHierarchy::HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName)
{
	if (Container == nullptr)
	{
		return;
	}

	switch (InElementType)
	{
		case ERigElementType::Bone:
		{
			for (FRigSpace& Space : Spaces)
			{
				if (Space.SpaceType == ERigSpaceType::Bone && Space.ParentName == InOldName)
				{
					Space.ParentIndex = Container->BoneHierarchy.GetIndex(InNewName);
					Space.ParentName = Space.ParentIndex == INDEX_NONE ? NAME_None : InNewName;
					Space.SpaceType = Space.ParentIndex == INDEX_NONE ? ERigSpaceType::Global : Space.SpaceType;
#if WITH_EDITOR
					OnSpaceReparented.Broadcast(Container, Space.GetElementKey(), InOldName, Space.ParentName);
#endif
				}
			}
			break;
		}
		case ERigElementType::Control:
		{
			for (FRigSpace& Space : Spaces)
			{
				if (Space.SpaceType == ERigSpaceType::Control && Space.ParentName == InOldName)
				{
					Space.ParentIndex = Container->ControlHierarchy.GetIndex(InNewName);
					Space.ParentName = Space.ParentIndex == INDEX_NONE ? NAME_None : InNewName;
					Space.SpaceType = Space.ParentIndex == INDEX_NONE ? ERigSpaceType::Global : Space.SpaceType;
#if WITH_EDITOR
					OnSpaceReparented.Broadcast(Container, Space.GetElementKey(), InOldName, Space.ParentName);
#endif
				}
			}
			break;
		}
		case ERigElementType::Space:
		case ERigElementType::Curve:
		{
			break;
		}
	}
}

#endif
// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigSpaceHierarchy.h"
#include "ControlRig.h"
#include "HelperUtil.h"

////////////////////////////////////////////////////////////////////////////////
// FRigSpaceHierarchy
////////////////////////////////////////////////////////////////////////////////

FRigSpaceHierarchy::FRigSpaceHierarchy()
#if WITH_EDITOR
	:Container(nullptr)
#endif
{
}

FRigSpaceHierarchy& FRigSpaceHierarchy::operator= (const FRigSpaceHierarchy &InOther)
{
#if WITH_EDITOR
	for (int32 Index = Num() - 1; Index >= 0; Index--)
	{
		FRigSpace SpaceToRemove = Spaces[Index];
		OnSpaceRemoved.Broadcast(Container, RigElementType(), SpaceToRemove.Name);
	}
#endif

	Spaces.Reset();
	Spaces.Append(InOther.Spaces);
	NameToIndexMapping.Reset();
	RefreshMapping();

#if WITH_EDITOR
	for (const FRigSpace& SpaceAdded : Spaces)
	{
		OnSpaceAdded.Broadcast(Container, RigElementType(), SpaceAdded.Name);
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
	NewSpace.SpaceType = InSpaceType;
	NewSpace.ParentName = InParentName;
	NewSpace.ParentIndex = INDEX_NONE;
	NewSpace.InitialTransform = InTransform;
	NewSpace.LocalTransform = InTransform;
	Spaces.Add(NewSpace);
	RefreshMapping();

#if WITH_EDITOR
	OnSpaceAdded.Broadcast(Container, RigElementType(), NewSpace.Name);
#endif

	int32 Index = GetIndex(NewSpace.Name);
	return Spaces[Index];
}

void FRigSpaceHierarchy::Reparent(const FName& InName, const FName& InNewParentName)
{
	int32 Index = GetIndex(InName);
	if (Index != INDEX_NONE)
	{
		Spaces[Index].ParentName = InNewParentName;
		Spaces[Index].ParentIndex = INDEX_NONE;
	}
}

FRigSpace FRigSpaceHierarchy::Remove(const FName& InNameToRemove)
{
	int32 IndexToDelete = GetIndex(InNameToRemove);
	FRigSpace RemovedSpace = Spaces[IndexToDelete];
	Spaces.RemoveAt(IndexToDelete);

	RefreshMapping();

#if WITH_EDITOR
	OnSpaceRemoved.Broadcast(Container, RigElementType(), RemovedSpace.Name);
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
	if (Spaces.IsValidIndex(InIndex))
	{
		FRigSpace& Space = Spaces[InIndex];
		// todo: get the space's transform + inverse
		//Space.GlobalTransform = InTransform;
		//Space.GlobalTransform.NormalizeRotation();
	}
}

FTransform FRigSpaceHierarchy::GetGlobalTransform(const FName& InName) const
{
	return GetGlobalTransform(GetIndex(InName));
}

FTransform FRigSpaceHierarchy::GetGlobalTransform(int32 InIndex) const
{
	if (Spaces.IsValidIndex(InIndex))
	{
		// todo: get the space's transform and compute
		//return Spaces[InIndex].GlobalTransform;
		return FTransform::Identity;
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

FName FRigSpaceHierarchy::Rename(const FName& InOldName, const FName& InNewName)
{
	if (InOldName != InNewName)
	{
		const int32 Found = GetIndex(InOldName);
		if (Found != INDEX_NONE)
		{
			FName NewName = GetSafeNewName(InNewName);
			Spaces[Found].Name = NewName;

			// go through find all children and rename them
			for (int32 Index = 0; Index < Spaces.Num(); ++Index)
			{
				if (Spaces[Index].ParentName == InOldName)
				{
					Spaces[Index].ParentName = NewName;
				}
			}

			RefreshMapping();

#if WITH_EDITOR
			OnSpaceRenamed.Broadcast(Container, RigElementType(), InOldName, NewName);
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

	// update parent index
	for (int32 Index = 0; Index < Spaces.Num(); ++Index)
	{
		// todo
		//Spaces[Index].ParentIndex = GetIndex(Spaces[Index].ParentName);
	}

	// initialize transform
	for (int32 Index = 0; Index < Spaces.Num(); ++Index)
	{
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

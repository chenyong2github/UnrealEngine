// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	NewSpace.ParentIndex = GetParentIndex(InSpaceType, InParentName);
	NewSpace.SpaceType = NewSpace.ParentIndex == INDEX_NONE ? ERigSpaceType::Global : InSpaceType;
	NewSpace.ParentName = NewSpace.ParentIndex == INDEX_NONE ? NAME_None : InParentName;
	NewSpace.InitialTransform = InTransform;
	NewSpace.LocalTransform = InTransform;
	FName NewSpaceName = NewSpace.Name;
	Spaces.Add(NewSpace);
	RefreshMapping();

#if WITH_EDITOR
	OnSpaceAdded.Broadcast(Container, RigElementType(), NewSpaceName);
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
			OnSpaceReparented.Broadcast(Container, RigElementType(), InName, OldParentName, NewParentName);
		}
#endif		
		return Spaces[GetIndex(InName)].ParentName == InNewParentName;
	}
	return false;
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
	if (Container == nullptr)
	{
		SetLocalTransform(InIndex, InTransform);
		return;
	}

	if (Spaces.IsValidIndex(InIndex))
	{
		FRigSpace& Space = Spaces[InIndex];
		FTransform ParentTransform = FTransform::Identity;

		switch (Space.SpaceType)
		{
			case ERigSpaceType::Global:
			{
				SetLocalTransform(InIndex, InTransform);
				return;
			}
			case ERigSpaceType::Bone:
			{
				ParentTransform = Container->GetGlobalTransform(ERigElementType::Bone, Space.ParentIndex);
				break;
			}
			case ERigSpaceType::Space:
			{
				ParentTransform = Container->GetGlobalTransform(ERigElementType::Space, Space.ParentIndex);
				break;
			}
			case ERigSpaceType::Control:
			{
				ParentTransform = Container->GetGlobalTransform(ERigElementType::Control, Space.ParentIndex);
				break;
			}
		}

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
				return Space.LocalTransform * Container->GetGlobalTransform(ERigElementType::Bone, Space.ParentIndex);
			}
			case ERigSpaceType::Space:
			{
				return Space.LocalTransform * Container->GetGlobalTransform(ERigElementType::Space, Space.ParentIndex);
			}
			case ERigSpaceType::Control:
			{
				return Space.LocalTransform * Container->GetGlobalTransform(ERigElementType::Control, Space.ParentIndex);
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
				return Container->GetIndex(ERigElementType::Bone, InName);
			}
			case ERigSpaceType::Space:
			{
				return Container->GetIndex(ERigElementType::Space, InName);
			}
			case ERigSpaceType::Control:
			{
				return Container->GetIndex(ERigElementType::Control, InName);
			}
		}
	}

	return INDEX_NONE;
}

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigHierarchyContainer.h"
#include "ControlRig.h"
#include "HelperUtil.h"

////////////////////////////////////////////////////////////////////////////////
// FRigHierarchyContainer
////////////////////////////////////////////////////////////////////////////////

FRigHierarchyContainer::FRigHierarchyContainer()
{
	Initialize();
}

FRigHierarchyContainer& FRigHierarchyContainer::operator= (const FRigHierarchyContainer &InOther)
{
	BoneHierarchy = InOther.BoneHierarchy;
	SpaceHierarchy = InOther.SpaceHierarchy;
	ControlHierarchy = InOther.ControlHierarchy;
	CurveContainer = InOther.CurveContainer;
	return *this;
}

void FRigHierarchyContainer::Initialize()
{
	BoneHierarchy.Container = this;
	SpaceHierarchy.Container = this;
	ControlHierarchy.Container = this;
	CurveContainer.Container = this;

#if WITH_EDITOR
	BoneHierarchy.OnBoneAdded.RemoveAll(this);
	BoneHierarchy.OnBoneRemoved.RemoveAll(this);
	BoneHierarchy.OnBoneRenamed.RemoveAll(this);
	BoneHierarchy.OnBoneReparented.RemoveAll(this);
	BoneHierarchy.OnBoneSelected.RemoveAll(this);

	BoneHierarchy.OnBoneAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	BoneHierarchy.OnBoneRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	BoneHierarchy.OnBoneRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
	BoneHierarchy.OnBoneReparented.AddRaw(this, &FRigHierarchyContainer::HandleOnElementReparented);
	BoneHierarchy.OnBoneSelected.AddRaw(this, &FRigHierarchyContainer::HandleOnElementSelected);

	SpaceHierarchy.OnSpaceAdded.RemoveAll(this);
	SpaceHierarchy.OnSpaceRemoved.RemoveAll(this);
	SpaceHierarchy.OnSpaceRenamed.RemoveAll(this);
	SpaceHierarchy.OnSpaceReparented.RemoveAll(this);
	SpaceHierarchy.OnSpaceSelected.RemoveAll(this);

	SpaceHierarchy.OnSpaceAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	SpaceHierarchy.OnSpaceRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	SpaceHierarchy.OnSpaceRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
	SpaceHierarchy.OnSpaceReparented.AddRaw(this, &FRigHierarchyContainer::HandleOnElementReparented);
	SpaceHierarchy.OnSpaceSelected.AddRaw(this, &FRigHierarchyContainer::HandleOnElementSelected);

	ControlHierarchy.OnControlAdded.RemoveAll(this);
	ControlHierarchy.OnControlRemoved.RemoveAll(this);
	ControlHierarchy.OnControlRenamed.RemoveAll(this);
	ControlHierarchy.OnControlReparented.RemoveAll(this);
	ControlHierarchy.OnControlSelected.RemoveAll(this);

	ControlHierarchy.OnControlAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	ControlHierarchy.OnControlRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	ControlHierarchy.OnControlRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
	ControlHierarchy.OnControlReparented.AddRaw(this, &FRigHierarchyContainer::HandleOnElementReparented);
	ControlHierarchy.OnControlSelected.AddRaw(this, &FRigHierarchyContainer::HandleOnElementSelected);

	CurveContainer.OnCurveAdded.RemoveAll(this);
	CurveContainer.OnCurveRemoved.RemoveAll(this);
	CurveContainer.OnCurveRenamed.RemoveAll(this);
	CurveContainer.OnCurveSelected.RemoveAll(this);

	CurveContainer.OnCurveAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	CurveContainer.OnCurveRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	CurveContainer.OnCurveRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
	CurveContainer.OnCurveSelected.AddRaw(this, &FRigHierarchyContainer::HandleOnElementSelected);

	// wire them between each other
	BoneHierarchy.OnBoneRenamed.RemoveAll(&SpaceHierarchy);
	SpaceHierarchy.OnSpaceRenamed.RemoveAll(&ControlHierarchy);
	ControlHierarchy.OnControlRenamed.RemoveAll(&SpaceHierarchy);
	BoneHierarchy.OnBoneRenamed.AddRaw(&SpaceHierarchy, &FRigSpaceHierarchy::HandleOnElementRenamed);
	SpaceHierarchy.OnSpaceRenamed.AddRaw(&ControlHierarchy, &FRigControlHierarchy::HandleOnElementRenamed);
	ControlHierarchy.OnControlRenamed.AddRaw(&SpaceHierarchy, &FRigSpaceHierarchy::HandleOnElementRenamed);

#endif

	BoneHierarchy.Initialize();
	SpaceHierarchy.Initialize();
	ControlHierarchy.Initialize();
	CurveContainer.Initialize();

	ResetTransforms();
}

void FRigHierarchyContainer::Reset()
{
	BoneHierarchy.Reset();
	SpaceHierarchy.Reset();
	ControlHierarchy.Reset();
	CurveContainer.Reset();

	Initialize();
}

void FRigHierarchyContainer::ResetTransforms()
{
	BoneHierarchy.ResetTransforms();
	SpaceHierarchy.ResetTransforms();
	ControlHierarchy.ResetTransforms();
	CurveContainer.ResetValues();
}

FTransform FRigHierarchyContainer::GetInitialTransform(ERigElementType InElementType, int32 InIndex) const
{
	if(InIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.GetInitialTransform(InIndex);
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.GetInitialTransform(InIndex);
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.GetInitialTransform(InIndex);
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}

	return FTransform::Identity;
}

#if WITH_EDITOR

void FRigHierarchyContainer::SetInitialTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform)
{
	if (InIndex == INDEX_NONE)
	{
		return;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			BoneHierarchy.SetInitialTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Space:
		{
			SpaceHierarchy.SetInitialTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Control:
		{
			ControlHierarchy.SetInitialTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}
}

#endif

FTransform FRigHierarchyContainer::GetLocalTransform(ERigElementType InElementType, int32 InIndex) const
{
	if(InIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.GetLocalTransform(InIndex);
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.GetLocalTransform(InIndex);
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.GetLocalTransform(InIndex);
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}

	return FTransform::Identity;
}

void FRigHierarchyContainer::SetLocalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform)
{
	if (InIndex == INDEX_NONE)
	{
		return;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			BoneHierarchy.SetLocalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Space:
		{
			SpaceHierarchy.SetLocalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Control:
		{
			ControlHierarchy.SetLocalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}
}

FTransform FRigHierarchyContainer::GetGlobalTransform(ERigElementType InElementType, int32 InIndex) const
{
	if(InIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.GetGlobalTransform(InIndex);
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.GetGlobalTransform(InIndex);
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.GetGlobalTransform(InIndex);
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}

	return FTransform::Identity;
}

void FRigHierarchyContainer::SetGlobalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform)
{
	if (InIndex == INDEX_NONE)
	{
		return;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			BoneHierarchy.SetGlobalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Space:
		{
			SpaceHierarchy.SetGlobalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Control:
		{
			ControlHierarchy.SetGlobalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}
}

#if WITH_EDITOR

void FRigHierarchyContainer::HandleOnElementAdded(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InName)
{
	// todo
	OnElementAdded.Broadcast(InContainer, InElementType, InName);
	OnElementChanged.Broadcast(InContainer, InElementType, InName);
}

void FRigHierarchyContainer::HandleOnElementRemoved(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InName)
{
	// todo
	OnElementRemoved.Broadcast(InContainer, InElementType, InName);
	OnElementChanged.Broadcast(InContainer, InElementType, InName);
}

void FRigHierarchyContainer::HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName)
{
	// todo
	OnElementRenamed.Broadcast(InContainer, InElementType, InOldName, InNewName);
	OnElementChanged.Broadcast(InContainer, InElementType, InNewName);
}

void FRigHierarchyContainer::HandleOnElementReparented(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InName, const FName& InOldParentName, const FName& InNewParentName)
{
	// todo
	OnElementReparented.Broadcast(InContainer, InElementType, InName, InOldParentName, InNewParentName);
	OnElementChanged.Broadcast(InContainer, InElementType, InName);
}

void FRigHierarchyContainer::HandleOnElementSelected(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InName, bool bSelected)
{
	OnElementSelected.Broadcast(InContainer, InElementType, InName, bSelected);
	OnElementChanged.Broadcast(InContainer, InElementType, InName);
}

#endif

bool FRigHierarchyContainer::IsParentedTo(ERigElementType InChildType, int32 InChildIndex, ERigElementType InParentType, int32 InParentIndex) const
{
	ensure(InChildIndex != INDEX_NONE);

	if (InParentIndex == INDEX_NONE)
	{
		return false;
	}

	switch (InChildType)
	{
		case ERigElementType::Curve:
		{
			return false;
		}
		case ERigElementType::Bone:
		{
			switch (InParentType)
			{
				case ERigElementType::Bone:
				{
					if (BoneHierarchy[InChildIndex].ParentIndex != INDEX_NONE)
					{
						if (BoneHierarchy[InChildIndex].ParentIndex == InParentIndex)
						{
							return true;
						}
						return IsParentedTo(ERigElementType::Bone, BoneHierarchy[InChildIndex].ParentIndex, InParentType, InParentIndex);
					}
					// no break - fall through to next case
				}
				case ERigElementType::Space:
				case ERigElementType::Control:
				case ERigElementType::Curve:
				{
					return false;
				}
			}
		}
		case ERigElementType::Space:
		{
			const FRigSpace& ChildSpace = SpaceHierarchy[InChildIndex];
			switch (ChildSpace.SpaceType)
			{
				case ERigSpaceType::Global:
				{
					return false;
				}
				case ERigSpaceType::Bone:
				{
					if (ChildSpace.ParentIndex == InParentIndex && InParentType == ERigElementType::Bone)
					{
						return true;
					}
					return IsParentedTo(ERigElementType::Bone, ChildSpace.ParentIndex, InParentType, InParentIndex);
				}
				case ERigSpaceType::Space:
				{
					if (ChildSpace.ParentIndex == InParentIndex && InParentType == ERigElementType::Space)
					{
						return true;
					}
					return IsParentedTo(ERigElementType::Space, ChildSpace.ParentIndex, InParentType, InParentIndex);
				}
				case ERigSpaceType::Control:
				{
					if (ChildSpace.ParentIndex == InParentIndex && InParentType == ERigElementType::Control)
					{
						return true;
					}
					return IsParentedTo(ERigElementType::Control, ChildSpace.ParentIndex, InParentType, InParentIndex);
				}
			}
		}
		case ERigElementType::Control:
		{
			const FRigControl& ChildControl = ControlHierarchy[InChildIndex];
			switch (InParentType)
			{
				case ERigElementType::Space:
				{
					if (ChildControl.SpaceIndex == InParentIndex)
					{
						return true;
					}
					// no break - fall through to next cases
				}
				case ERigElementType::Control:
				case ERigElementType::Bone:
				{
					return IsParentedTo(ERigElementType::Space, ChildControl.SpaceIndex, InParentType, InParentIndex);
				}
				case ERigElementType::Curve:
				{
					return false;
				}
			}
		}
	}

	return false;
}

#if WITH_EDITOR

bool FRigHierarchyContainer::Select(const FName& InName, ERigElementType InElementType, bool bSelect)
{
	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.Select(InName, bSelect);
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.Select(InName, bSelect);
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.Select(InName, bSelect);
		}
		case ERigElementType::Curve:
		{
			return CurveContainer.Select(InName, bSelect);
		}
	}
	return false;
}

bool FRigHierarchyContainer::ClearSelection(ERigElementType InElementType)
{
	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.ClearSelection();
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.ClearSelection();
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.ClearSelection();
		}
		case ERigElementType::Curve:
		{
			return CurveContainer.ClearSelection();
		}
	}
	return false;
}

bool FRigHierarchyContainer::IsSelected(const FName& InName, ERigElementType InElementType) const
{
	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.IsSelected(InName);
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.IsSelected(InName);
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.IsSelected(InName);
		}
		case ERigElementType::Curve:
		{
			return CurveContainer.IsSelected(InName);
		}
	}
	return false;
}

#endif
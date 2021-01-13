// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigDataTypes.h"
#include "IKRigHierarchy.h"

FIKRigTransforms::FIKRigTransforms(const FIKRigHierarchy* InHierarchy)
	: Hierarchy(InHierarchy)
{
	ensure(Hierarchy != nullptr);
}

// we don't want to allocate local transforms until required
// so we don't do this in the constructor but on demand when to be used
void FIKRigTransforms::EnsureLocalTransformsExist()
{
	// first check validation
	if (Hierarchy&& Hierarchy->GetNum() == GlobalTransforms.Num())
	{
		if (LocalTransforms.Num() != GlobalTransforms.Num())
		{
			LocalTransforms.SetNumUninitialized(GlobalTransforms.Num());
			LocalTransformDirtyFlags.Init(true, GlobalTransforms.Num());
		}
	}
}

void FIKRigTransforms::RecalculateLocalTransform()
{
	// make sure to call EnsureLocalTransformsExist before getting here
	check (Hierarchy && LocalTransforms.Num() == GlobalTransforms.Num());
	
	for (TConstSetBitIterator<> It(LocalTransformDirtyFlags); It; ++It)
	{
		const int32 BoneIndex = It.GetIndex();
		const int32 ParentIndex = Hierarchy->GetParentIndex(BoneIndex);
		LocalTransforms[BoneIndex] = GetRelativeTransform(BoneIndex, ParentIndex);
	}

	// clear the bits
	LocalTransformDirtyFlags.Init(false, GlobalTransforms.Num());
}

void FIKRigTransforms::UpdateLocalTransform(int32 Index)
{
	EnsureLocalTransformsExist();

	if (Hierarchy && Hierarchy->IsValidIndex(Index) && LocalTransformDirtyFlags[Index])
	{
		int32 ParentIndex = Hierarchy->GetParentIndex(Index);
		LocalTransforms[Index] = GetRelativeTransform(Index, ParentIndex);
		LocalTransformDirtyFlags[Index] = false;
	}
}

void FIKRigTransforms::SetGlobalTransform(int32 Index, const FTransform& InTransform, bool bPropagate)
{
	if (GlobalTransforms.IsValidIndex(Index))
	{
		if (bPropagate)
		{
			// update all children
			SetGlobalTransform_Internal(Index, InTransform);
		}
		else
		{
			GlobalTransforms[Index] = InTransform;

			// if we have local transform, we also update
			if (LocalTransforms.IsValidIndex(Index))
			{
				LocalTransformDirtyFlags[Index] = true;
				UpdateLocalTransform(Index);
			}
		}
	}
}

void FIKRigTransforms::SetLocalTransform(int32 Index, const FTransform& InTransform, bool bPropagate)
{
	// we go through global 
	// because we can't just update local only
	if (Hierarchy && Hierarchy->IsValidIndex(Index))
	{
		const int32 ParentIndex = Hierarchy->GetParentIndex(Index);
		// if parent is invalid, it will get identity
		FTransform NewTransform = InTransform * GetGlobalTransform(ParentIndex);
		SetGlobalTransform(Index, NewTransform, true);
	}
}

const FTransform& FIKRigTransforms::GetLocalTransform(int32 Index) const
{
	if (Hierarchy && Hierarchy->IsValidIndex(Index))
	{
		// this is because this requires to modify LocalTransforms
		const_cast<FIKRigTransforms*>(this)->UpdateLocalTransform(Index);
		return LocalTransforms[Index];
	}

	return FTransform::Identity;
}

const FTransform& FIKRigTransforms::GetGlobalTransform(int32 Index) const
{
	if (GlobalTransforms.IsValidIndex(Index))
	{
		return GlobalTransforms[Index];
	}

	return FTransform::Identity;
}
/* This function does propagate through children
	* of the current index and keeps last LocalTransform to up to date
	*/
void FIKRigTransforms::SetGlobalTransform_Internal(int32 Index, const FTransform& InTransform)
{
	// first calculate local transform if not found
	EnsureLocalTransformsExist();
	RecalculateLocalTransform();

	// set global transform recursively without modifying Local Transform
	SetGlobalTransform_Recursive(Index, InTransform);

	// now update my local transform
	LocalTransformDirtyFlags[Index] = true;
	UpdateLocalTransform(Index);
}

/** Set Global Transform Recursive
	*
	*/
void FIKRigTransforms::SetGlobalTransform_Recursive(int32 Index, const FTransform& InTransform)
{
	GlobalTransforms[Index] = InTransform;
	TArray<int32> Children = Hierarchy->FindChildren(Index);
	for (int32 Child : Children)
	{
		// calculate new global based on local 
		FTransform NewTransform = LocalTransforms[Child] * InTransform;
		SetGlobalTransform_Recursive(Child, NewTransform);
	}
}

void FIKRigTransforms::SetAllGlobalTransforms(const TArray<FTransform>& InTransforms)
{
	if (Hierarchy && Hierarchy->GetNum() == InTransforms.Num())
	{
		GlobalTransforms = InTransforms;

		// we don't want previous transform data
		LocalTransforms.Reset();
		LocalTransformDirtyFlags.Reset();
	}
}

FTransform FIKRigTransforms::GetRelativeTransform(int32 ChildIndex, int32 ParentIndex) const
{
	if (GlobalTransforms.IsValidIndex(ChildIndex))
	{
		if (GlobalTransforms.IsValidIndex(ParentIndex))
		{
			return (GlobalTransforms[ChildIndex].GetRelativeTransform(GlobalTransforms[ParentIndex]));
		}
		else
		{
			return GlobalTransforms[ChildIndex];
		}
	}

	return FTransform::Identity;
}
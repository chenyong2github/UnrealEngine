// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IKRigDataTypes.cpp: IKRig Data Type impelmentation file
=============================================================================*/

#include "IKRigDataTypes.h"
#include "IKRigHierarchy.h"

FIKRigTransformModifier::FIKRigTransformModifier(const FIKRigHierarchy* InHierarchy)
	: Hierarchy(InHierarchy)
{
	ensure(Hierarchy != nullptr);
}

// we don't want to allocate local transforms until required
// so we don't do this in the constructor but on demand when to be used
void FIKRigTransformModifier::EnsureLocalTransformsExist()
{
	// first check validation
	if (Hierarchy&& Hierarchy->GetNum() == GlobalTransforms.GetNum())
	{
		if (LocalTransforms.Num() != GlobalTransforms.GetNum())
		{
			LocalTransforms.SetNumUninitialized(GlobalTransforms.GetNum());
			LocalTransformDirtyFlags.Init(true, GlobalTransforms.GetNum());
		}
	}
}

void FIKRigTransformModifier::RecalculateLocalTransform()
{
	// make sure to call EnsureLocalTransformsExist before getting here
	check (Hierarchy && LocalTransforms.Num() == GlobalTransforms.GetNum());
	
	for (TConstSetBitIterator<> It(LocalTransformDirtyFlags); It; ++It)
	{
		const int32 BoneIndex = It.GetIndex();
		const int32 ParentIndex = Hierarchy->GetParentIndex(BoneIndex);
		LocalTransforms[BoneIndex] = GlobalTransforms.GetRelativeTransform(BoneIndex, ParentIndex);
	}

	// clear the bits
	LocalTransformDirtyFlags.Init(false, GlobalTransforms.GetNum());
}

void FIKRigTransformModifier::UpdateLocalTransform(int32 Index)
{
	EnsureLocalTransformsExist();

	if (Hierarchy && Hierarchy->IsValidIndex(Index) && LocalTransformDirtyFlags[Index])
	{
		int32 ParentIndex = Hierarchy->GetParentIndex(Index);
		LocalTransforms[Index] = GlobalTransforms.GetRelativeTransform(Index, ParentIndex);
		LocalTransformDirtyFlags[Index] = false;
	}
}

void FIKRigTransformModifier::SetGlobalTransform(int32 Index, const FTransform& InTransform, bool bPropagate)
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
			GlobalTransforms.GlobalTransforms[Index] = InTransform;

			// if we have local transform, we also update
			if (LocalTransforms.IsValidIndex(Index))
			{
				LocalTransformDirtyFlags[Index] = true;
				UpdateLocalTransform(Index);
			}
		}
	}
}

void FIKRigTransformModifier::SetLocalTransform(int32 Index, const FTransform& InTransform, bool bPropagate)
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

const FTransform& FIKRigTransformModifier::GetLocalTransform(int32 Index) const
{
	if (Hierarchy && Hierarchy->IsValidIndex(Index))
	{
		// this is because this requires to modify LocalTransforms
		const_cast<FIKRigTransformModifier*>(this)->UpdateLocalTransform(Index);
		return LocalTransforms[Index];
	}

	return FTransform::Identity;
}

const FTransform& FIKRigTransformModifier::GetGlobalTransform(int32 Index) const
{
	return GlobalTransforms.GetGlobalTransform(Index);
}
/* This function does propagate through children
	* of the current index and keeps last LocalTransform to up to date
	*/
void FIKRigTransformModifier::SetGlobalTransform_Internal(int32 Index, const FTransform& InTransform)
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
void FIKRigTransformModifier::SetGlobalTransform_Recursive(int32 Index, const FTransform& InTransform)
{
	GlobalTransforms.GlobalTransforms[Index] = InTransform;
	TArray<int32> Children = Hierarchy->FindChildren(Index);
	for (int32 Child : Children)
	{
		// calculate new global based on local 
		FTransform NewTransform = LocalTransforms[Child] * InTransform;
		SetGlobalTransform_Recursive(Child, NewTransform);
	}
}

void FIKRigTransformModifier::ResetGlobalTransform(const FIKRigTransform& InTransform)
{
	if (Hierarchy && Hierarchy->GetNum() == InTransform.GetNum())
	{
		GlobalTransforms = InTransform;

		// we don't want previous transform data
		LocalTransforms.Reset();
		LocalTransformDirtyFlags.Reset();
	}
}
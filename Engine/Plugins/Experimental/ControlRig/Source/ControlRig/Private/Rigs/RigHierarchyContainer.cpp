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
	return *this;
}

void FRigHierarchyContainer::Initialize()
{
#if WITH_EDITOR
	BoneHierarchy.Container = this;

	BoneHierarchy.OnBoneAdded.RemoveAll(this);
	BoneHierarchy.OnBoneRemoved.RemoveAll(this);
	BoneHierarchy.OnBoneRenamed.RemoveAll(this);
	BoneHierarchy.OnBoneReparented.RemoveAll(this);

	BoneHierarchy.OnBoneAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	BoneHierarchy.OnBoneRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	BoneHierarchy.OnBoneRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
	BoneHierarchy.OnBoneReparented.AddRaw(this, &FRigHierarchyContainer::HandleOnElementReparented);
#endif

	BoneHierarchy.Initialize();

	ResetTransforms();
}

void FRigHierarchyContainer::Reset()
{
	BoneHierarchy.Reset();
	
	Initialize();
}

void FRigHierarchyContainer::ResetTransforms()
{
	BoneHierarchy.ResetTransforms();
}

#if WITH_EDITOR

void FRigHierarchyContainer::HandleOnElementAdded(FRigHierarchyContainer* InContainer, ERigHierarchyElementType InElementType, const FName& InName)
{
	// todo
	OnElementAdded.Broadcast(InContainer, InElementType, InName);
	OnElementChanged.Broadcast(InContainer, InElementType, InName);
}

void FRigHierarchyContainer::HandleOnElementRemoved(FRigHierarchyContainer* InContainer, ERigHierarchyElementType InElementType, const FName& InName)
{
	// todo
	OnElementRemoved.Broadcast(InContainer, InElementType, InName);
	OnElementChanged.Broadcast(InContainer, InElementType, InName);
}

void FRigHierarchyContainer::HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigHierarchyElementType InElementType, const FName& InOldName, const FName& InNewName)
{
	// todo
	OnElementRenamed.Broadcast(InContainer, InElementType, InOldName, InNewName);
	OnElementChanged.Broadcast(InContainer, InElementType, InNewName);
}

void FRigHierarchyContainer::HandleOnElementReparented(FRigHierarchyContainer* InContainer, ERigHierarchyElementType InElementType, const FName& InName, const FName& InOldParentName, const FName& InNewParentName)
{
	// todo
	OnElementReparented.Broadcast(InContainer, InElementType, InName, InOldParentName, InNewParentName);
	OnElementChanged.Broadcast(InContainer, InElementType, InName);
}

#endif

////////////////////////////////////////////////////////////////////////////////
// FRigHierarchyRef
////////////////////////////////////////////////////////////////////////////////

FRigHierarchyRef::FRigHierarchyRef()
: Container(nullptr)
{

}

FRigBoneHierarchy* FRigHierarchyRef::GetBones()
{
	return GetBonesInternal();
}

const FRigBoneHierarchy* FRigHierarchyRef::GetBones() const
{
	return (const FRigBoneHierarchy*)GetBonesInternal();
}

FRigBoneHierarchy* FRigHierarchyRef::GetBonesInternal() const
{
	if (Container)
	{
		return &Container->BoneHierarchy;
	}
	return nullptr;
}


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
#if WITH_EDITOR
	BoneHierarchy.Container = this;
	SpaceHierarchy.Container = this;
	ControlHierarchy.Container = this;
	CurveContainer.Container = this;

	BoneHierarchy.OnBoneAdded.RemoveAll(this);
	BoneHierarchy.OnBoneRemoved.RemoveAll(this);
	BoneHierarchy.OnBoneRenamed.RemoveAll(this);
	BoneHierarchy.OnBoneReparented.RemoveAll(this);

	BoneHierarchy.OnBoneAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	BoneHierarchy.OnBoneRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	BoneHierarchy.OnBoneRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
	BoneHierarchy.OnBoneReparented.AddRaw(this, &FRigHierarchyContainer::HandleOnElementReparented);

	SpaceHierarchy.OnSpaceAdded.RemoveAll(this);
	SpaceHierarchy.OnSpaceRemoved.RemoveAll(this);
	SpaceHierarchy.OnSpaceRenamed.RemoveAll(this);

	SpaceHierarchy.OnSpaceAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	SpaceHierarchy.OnSpaceRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	SpaceHierarchy.OnSpaceRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);

	ControlHierarchy.OnControlAdded.RemoveAll(this);
	ControlHierarchy.OnControlRemoved.RemoveAll(this);
	ControlHierarchy.OnControlRenamed.RemoveAll(this);
	ControlHierarchy.OnControlReparented.RemoveAll(this);

	ControlHierarchy.OnControlAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	ControlHierarchy.OnControlRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	ControlHierarchy.OnControlRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
	ControlHierarchy.OnControlReparented.AddRaw(this, &FRigHierarchyContainer::HandleOnElementReparented);

	CurveContainer.OnCurveAdded.RemoveAll(this);
	CurveContainer.OnCurveRemoved.RemoveAll(this);
	CurveContainer.OnCurveRenamed.RemoveAll(this);

	CurveContainer.OnCurveAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	CurveContainer.OnCurveRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	CurveContainer.OnCurveRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
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

#endif

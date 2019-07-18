// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigBoneHierarchy.h"
#include "RigSpaceHierarchy.h"
#include "RigControlHierarchy.h"
#include "RigCurveContainer.h"
#include "RigHierarchyContainer.generated.h"

class UControlRig;
struct FRigHierarchyContainer;

USTRUCT()
struct CONTROLRIG_API FRigHierarchyContainer
{
public:
	GENERATED_BODY()

	FRigHierarchyContainer();
	FRigHierarchyContainer& operator= (const FRigHierarchyContainer &InOther);

	UPROPERTY()
	FRigBoneHierarchy BoneHierarchy;

	UPROPERTY()
	FRigSpaceHierarchy SpaceHierarchy;

	UPROPERTY()
	FRigControlHierarchy ControlHierarchy;

	UPROPERTY()
	FRigCurveContainer CurveContainer;

	void Initialize();
	void Reset();
	void ResetTransforms();

#if WITH_EDITOR
	FRigElementChanged OnElementChanged;
	FRigElementAdded OnElementAdded;
	FRigElementRemoved OnElementRemoved;
	FRigElementRenamed OnElementRenamed;
	FRigElementReparented OnElementReparented;
#endif

protected:

	// disable copy constructor
	FRigHierarchyContainer(const FRigHierarchyContainer& InContainer) {}

#if WITH_EDITOR
	void HandleOnElementAdded(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InName);
	void HandleOnElementRemoved(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InName);
	void HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName);
	void HandleOnElementReparented(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InName, const FName& InOldParentName, const FName& InNewParentName);
#endif
};

// this struct is still here for backwards compatibility - but not used anywhere
USTRUCT()
struct CONTROLRIG_API FRigHierarchyRef
{
	GENERATED_BODY()
};
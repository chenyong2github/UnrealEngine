// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigBoneHierarchy.h"
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

	void Initialize();
	void Reset();
	void ResetTransforms();

#if WITH_EDITOR
	FRigHierarchyElementChanged OnElementChanged;
	FRigHierarchyElementAdded OnElementAdded;
	FRigHierarchyElementRemoved OnElementRemoved;
	FRigHierarchyElementRenamed OnElementRenamed;
	FRigHierarchyElementReparented OnElementReparented;
#endif

protected:

	// disable copy constructor
	FRigHierarchyContainer(const FRigHierarchyContainer& InContainer) {}

#if WITH_EDITOR
	void HandleOnElementAdded(FRigHierarchyContainer* InContainer, ERigHierarchyElementType InElementType, const FName& InName);
	void HandleOnElementRemoved(FRigHierarchyContainer* InContainer, ERigHierarchyElementType InElementType, const FName& InName);
	void HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigHierarchyElementType InElementType, const FName& InOldName, const FName& InNewName);
	void HandleOnElementReparented(FRigHierarchyContainer* InContainer, ERigHierarchyElementType InElementType, const FName& InName, const FName& InOldParentName, const FName& InNewParentName);
#endif
};

USTRUCT()
struct CONTROLRIG_API FRigHierarchyRef
{
	GENERATED_BODY()

	FRigHierarchyRef();

	FRigBoneHierarchy* GetBones();
	const FRigBoneHierarchy* GetBones() const;

private:

	struct FRigHierarchyContainer* Container;

	FRigBoneHierarchy* GetBonesInternal() const;

	friend class UControlRig;
	friend class FControlRigUnitTestBase;
};
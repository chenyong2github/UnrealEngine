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

	FORCEINLINE int32 GetIndex(ERigElementType InElementType, const FName& InName) const
	{
		switch(InElementType)
		{
			case ERigElementType::Bone:
			{
				return BoneHierarchy.GetIndex(InName);
			}
			case ERigElementType::Space:
			{
				return SpaceHierarchy.GetIndex(InName);
			}
			case ERigElementType::Control:
			{
				return ControlHierarchy.GetIndex(InName);
			}
			case ERigElementType::Curve:
			{
				return CurveContainer.GetIndex(InName);
			}
		}
		return INDEX_NONE;
	}

	FORCEINLINE FTransform GetInitialTransform(ERigElementType InElementType, const FName& InName) const
	{
		return GetInitialTransform(InElementType, GetIndex(InElementType, InName));
	}
	FTransform GetInitialTransform(ERigElementType InElementType, int32 InIndex) const;

#if WITH_EDITOR
	FORCEINLINE void SetInitialTransform(ERigElementType InElementType, const FName& InName, const FTransform& InTransform)
	{
		return SetInitialTransform(InElementType, GetIndex(InElementType, InName), InTransform);
	}
	void SetInitialTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform);
#endif

	FORCEINLINE FTransform GetLocalTransform(ERigElementType InElementType, const FName& InName) const
	{
		return GetLocalTransform(InElementType, GetIndex(InElementType, InName));
	}
	FTransform GetLocalTransform(ERigElementType InElementType, int32 InIndex) const;

	FORCEINLINE void SetLocalTransform(ERigElementType InElementType, const FName& InName, const FTransform& InTransform)
	{
		return SetLocalTransform(InElementType, GetIndex(InElementType, InName), InTransform);
	}
	void SetLocalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform);

	FORCEINLINE FTransform GetGlobalTransform(ERigElementType InElementType, const FName& InName) const
	{
		return GetGlobalTransform(InElementType, GetIndex(InElementType, InName));
	}
	FTransform GetGlobalTransform(ERigElementType InElementType, int32 InIndex) const;

	FORCEINLINE void SetGlobalTransform(ERigElementType InElementType, const FName& InName, const FTransform& InTransform)
	{
		return SetGlobalTransform(InElementType, GetIndex(InElementType, InName), InTransform);
	}
	void SetGlobalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform);

	bool IsParentedTo(ERigElementType InChildType, int32 InChildIndex, ERigElementType InParentType, int32 InParentIndex) const;

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
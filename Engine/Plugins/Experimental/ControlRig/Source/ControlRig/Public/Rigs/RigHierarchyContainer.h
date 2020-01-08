// Copyright Epic Games, Inc. All Rights Reserved.

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

UENUM()
enum class ERigHierarchyImportMode : uint8
{
	Append,

	Replace,

	ReplaceLocalTransform,

	ReplaceGlobalTransform,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

USTRUCT()
struct CONTROLRIG_API FRigHierarchyCopyPasteContent
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<ERigElementType> Types;

	UPROPERTY()
	TArray<FString> Contents;

	UPROPERTY()
	TArray<FTransform> LocalTransforms;

	UPROPERTY()
	TArray<FTransform> GlobalTransforms;
};

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

	FORCEINLINE int32 GetIndex(const FRigElementKey& InKey) const
	{
		switch(InKey.Type)
		{
			case ERigElementType::Bone:
			{
				return BoneHierarchy.GetIndex(InKey.Name);
			}
			case ERigElementType::Space:
			{
				return SpaceHierarchy.GetIndex(InKey.Name);
			}
			case ERigElementType::Control:
			{
				return ControlHierarchy.GetIndex(InKey.Name);
			}
			case ERigElementType::Curve:
			{
				return CurveContainer.GetIndex(InKey.Name);
			}
		}
		return INDEX_NONE;
	}

	FORCEINLINE FTransform GetInitialTransform(const FRigElementKey& InKey) const
	{
		return GetInitialTransform(InKey.Type, GetIndex(InKey));
	}
	FTransform GetInitialTransform(ERigElementType InElementType, int32 InIndex) const;

#if WITH_EDITOR
	FORCEINLINE void SetInitialTransform(const FRigElementKey& InKey, const FTransform& InTransform)
	{
		return SetInitialTransform(InKey.Type, GetIndex(InKey), InTransform);
	}
	void SetInitialTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform);
#endif

	FORCEINLINE FTransform GetInitialGlobalTransform(const FRigElementKey& InKey) const
	{
		return GetInitialGlobalTransform(InKey.Type, GetIndex(InKey));
	}
	FTransform GetInitialGlobalTransform(ERigElementType InElementType, int32 InIndex) const;

	FORCEINLINE void SetInitialGlobalTransform(const FRigElementKey& InKey, const FTransform& InTransform)
	{
		SetInitialGlobalTransform(InKey.Type, GetIndex(InKey), InTransform);
	}
	void SetInitialGlobalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform);

	FORCEINLINE FTransform GetLocalTransform(const FRigElementKey& InKey) const
	{
		return GetLocalTransform(InKey.Type, GetIndex(InKey));
	}
	FTransform GetLocalTransform(ERigElementType InElementType, int32 InIndex) const;

	FORCEINLINE void SetLocalTransform(const FRigElementKey& InKey, const FTransform& InTransform)
	{
		return SetLocalTransform(InKey.Type, GetIndex(InKey), InTransform);
	}
	void SetLocalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform);

	FORCEINLINE FTransform GetGlobalTransform(const FRigElementKey& InKey) const
	{
		return GetGlobalTransform(InKey.Type, GetIndex(InKey));
	}
	FTransform GetGlobalTransform(ERigElementType InElementType, int32 InIndex) const;

	FORCEINLINE void SetGlobalTransform(const FRigElementKey& InKey, const FTransform& InTransform)
	{
		return SetGlobalTransform(InKey.Type, GetIndex(InKey), InTransform);
	}
	void SetGlobalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform);

	bool IsParentedTo(ERigElementType InChildType, int32 InChildIndex, ERigElementType InParentType, int32 InParentIndex) const;

#if WITH_EDITOR

	FString ExportSelectionToText() const;
	FString ExportToText(const TArray<FRigElementKey>& InSelection) const;
	TArray<FRigElementKey> ImportFromText(const FString& InContent, ERigHierarchyImportMode InImportMode = ERigHierarchyImportMode::Append, bool bSelectNewElements = true);

	TArray<FRigElementKey> CurrentSelection() const;
	TArray<FRigElementKey> GetAllItems(bool bSort = false) const;
	bool Select(const FRigElementKey& InKey, bool bSelect = true);
	bool ClearSelection();
	bool ClearSelection(ERigElementType InElementType);
	bool IsSelected(const FRigElementKey& InKey) const;

	FRigElementChanged OnElementChanged;
	FRigElementAdded OnElementAdded;
	FRigElementRemoved OnElementRemoved;
	FRigElementRenamed OnElementRenamed;
	FRigElementReparented OnElementReparented;
	FRigElementSelected OnElementSelected;

#endif

protected:

	// disable copy constructor
	FRigHierarchyContainer(const FRigHierarchyContainer& InContainer) {}

#if WITH_EDITOR
	void HandleOnElementAdded(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey);
	void HandleOnElementRemoved(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey);
	void HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName);
	void HandleOnElementReparented(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName);
	void HandleOnElementSelected(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, bool bSelected);
#endif
};

// this struct is still here for backwards compatibility - but not used anywhere
USTRUCT()
struct CONTROLRIG_API FRigHierarchyRef
{
	GENERATED_BODY()
};
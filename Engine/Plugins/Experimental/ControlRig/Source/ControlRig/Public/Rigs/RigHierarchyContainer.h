// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigHierarchyPose.h"
#include "RigBoneHierarchy.h"
#include "RigSpaceHierarchy.h"
#include "RigControlHierarchy.h"
#include "RigCurveContainer.h"
#include "RigHierarchyCache.h"
#include "RigInfluenceMap.h"
#include "RigHierarchyContainer.generated.h"

class UControlRig;
struct FRigHierarchyContainer;

UENUM(BlueprintType)
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
struct CONTROLRIG_API FRigMirrorSettings
{
	GENERATED_USTRUCT_BODY()

	FRigMirrorSettings()
	: MirrorAxis(EAxis::X)
	, AxisToFlip(EAxis::Z)
	{
	}

	// the axis to mirror against
	UPROPERTY(EditAnywhere, Category = Settings)
	TEnumAsByte<EAxis::Type> MirrorAxis;

	// the axis to flip for rotations
	UPROPERTY(EditAnywhere, Category = Settings)
	TEnumAsByte<EAxis::Type> AxisToFlip;

	// the string to replace all occurences of with New Name
	UPROPERTY(EditAnywhere, Category = Settings)
	FString OldName;

	// the string to replace all occurences of Old Name with
	UPROPERTY(EditAnywhere, Category = Settings)
	FString NewName;

	FTransform MirrorTransform(const FTransform& InTransform) const;
	FVector MirrorVector(const FVector& InVector) const;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigHierarchyContainer
{
public:

	GENERATED_BODY()

	FRigHierarchyContainer();
	FRigHierarchyContainer(const FRigHierarchyContainer& InOther);
	FRigHierarchyContainer& operator= (const FRigHierarchyContainer& InOther);

	UPROPERTY()
	FRigBoneHierarchy BoneHierarchy;

	UPROPERTY()
	FRigSpaceHierarchy SpaceHierarchy;

	UPROPERTY()
	FRigControlHierarchy ControlHierarchy;

	UPROPERTY()
	FRigCurveContainer CurveContainer;

	UPROPERTY(transient)
	int32 Version;

	void Initialize(bool bResetTransforms = true);
	void Reset();
	void ResetTransforms();

	// copies all initial transforms from another hierarchy
	void CopyInitialTransforms(const FRigHierarchyContainer& InOther);

	// returns this hierarchy's current pose
	FRigPose GetPose() const;

	// sets the current hierarchy from the given pose
	void SetPose(FRigPose& InPose);

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
	FORCEINLINE FTransform GetInitialTransform(const FCachedRigElement& InCachedElement) const
	{
		if(InCachedElement.IsValid())
		{
			return GetInitialTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex());
		}
		return FTransform::Identity;
	}
	FORCEINLINE FTransform GetInitialTransform(FCachedRigElement& InCachedElement) const
	{
		if(InCachedElement.UpdateCache(this))
		{
			return GetInitialTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex());
		}
		return FTransform::Identity;
	}
	FTransform GetInitialTransform(ERigElementType InElementType, int32 InIndex) const;

	FORCEINLINE void SetInitialTransform(const FRigElementKey& InKey, const FTransform& InTransform)
	{
		SetInitialTransform(InKey.Type, GetIndex(InKey), InTransform);
	}
	FORCEINLINE void SetInitialTransform(const FCachedRigElement& InCachedElement, const FTransform& InTransform)
	{
		if(InCachedElement.IsValid())
		{
			SetInitialTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex(), InTransform);
		}
	}
	FORCEINLINE void SetInitialTransform(FCachedRigElement& InCachedElement, const FTransform& InTransform)
	{
		if(InCachedElement.UpdateCache(this))
		{
			SetInitialTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex(), InTransform);
		}
	}
	void SetInitialTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform);

	FORCEINLINE FTransform GetInitialGlobalTransform(const FRigElementKey& InKey) const
	{
		return GetInitialGlobalTransform(InKey.Type, GetIndex(InKey));
	}
	FORCEINLINE FTransform GetInitialGlobalTransform(const FCachedRigElement& InCachedElement) const
	{
		if(InCachedElement.IsValid())
		{
			return GetInitialGlobalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex());
		}
		return FTransform::Identity;
	}
	FORCEINLINE FTransform GetInitialGlobalTransform(FCachedRigElement& InCachedElement) const
	{
		if(InCachedElement.UpdateCache(this))
		{
			return GetInitialGlobalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex());
		}
		return FTransform::Identity;
	}
	FTransform GetInitialGlobalTransform(ERigElementType InElementType, int32 InIndex) const;

	FORCEINLINE void SetInitialGlobalTransform(const FRigElementKey& InKey, const FTransform& InTransform)
	{
		SetInitialGlobalTransform(InKey.Type, GetIndex(InKey), InTransform);
	}
	FORCEINLINE void SetInitialGlobalTransform(const FCachedRigElement& InCachedElement, const FTransform& InTransform)
	{
		if(InCachedElement.IsValid())
		{
			SetInitialGlobalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex(), InTransform);
		}
	}
	FORCEINLINE void SetInitialGlobalTransform(FCachedRigElement& InCachedElement, const FTransform& InTransform)
	{
		if(InCachedElement.UpdateCache(this))
		{
			SetInitialGlobalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex(), InTransform);
		}
	}
	void SetInitialGlobalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform);

	FORCEINLINE FTransform GetLocalTransform(const FRigElementKey& InKey) const
	{
		return GetLocalTransform(InKey.Type, GetIndex(InKey));
	}
	FORCEINLINE FTransform GetLocalTransform(const FCachedRigElement& InCachedElement) const
	{
		if(InCachedElement.IsValid())
		{
			return GetLocalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex());
		}
		return FTransform::Identity;
	}
	FORCEINLINE FTransform GetLocalTransform(FCachedRigElement& InCachedElement) const
	{
		if(InCachedElement.UpdateCache(this))
		{
			return GetLocalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex());
		}
		return FTransform::Identity;
	}
	FTransform GetLocalTransform(ERigElementType InElementType, int32 InIndex) const;

	FORCEINLINE void SetLocalTransform(const FRigElementKey& InKey, const FTransform& InTransform, bool bPropagateToChildren = true)
	{
		SetLocalTransform(InKey.Type, GetIndex(InKey), InTransform, bPropagateToChildren);
	}
	FORCEINLINE void SetLocalTransform(const FCachedRigElement& InCachedElement, const FTransform& InTransform, bool bPropagateToChildren = true)
	{
		if(InCachedElement.IsValid())
		{
			SetLocalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex(), InTransform, bPropagateToChildren);
		}
	}
	FORCEINLINE void SetLocalTransform(FCachedRigElement& InCachedElement, const FTransform& InTransform, bool bPropagateToChildren = true)
	{
		if(InCachedElement.UpdateCache(this))
		{
			SetLocalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex(), InTransform, bPropagateToChildren);
		}
	}
	void SetLocalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform, bool bPropagateToChildren = true);

	FORCEINLINE FTransform GetGlobalTransform(const FRigElementKey& InKey) const
	{
		return GetGlobalTransform(InKey.Type, GetIndex(InKey));
	}
	FORCEINLINE FTransform GetGlobalTransform(const FCachedRigElement& InCachedElement) const
	{
		if(InCachedElement.IsValid())
		{
			return GetGlobalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex());
		}
		return FTransform::Identity;
	}
	FORCEINLINE FTransform GetGlobalTransform(FCachedRigElement& InCachedElement) const
	{
		if(InCachedElement.UpdateCache(this))
		{
			return GetGlobalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex());
		}
		return FTransform::Identity;
	}
	FTransform GetGlobalTransform(ERigElementType InElementType, int32 InIndex) const;

	FORCEINLINE void SetGlobalTransform(const FRigElementKey& InKey, const FTransform& InTransform, bool bPropagateToChildren = true)
	{
		SetGlobalTransform(InKey.Type, GetIndex(InKey), InTransform, bPropagateToChildren);
	}
	FORCEINLINE void SetGlobalTransform(const FCachedRigElement& InCachedElement, const FTransform& InTransform, bool bPropagateToChildren = true)
	{
		if(InCachedElement.IsValid())
		{
			SetGlobalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex(), InTransform, bPropagateToChildren);
		}
	}
	FORCEINLINE void SetGlobalTransform(FCachedRigElement& InCachedElement, const FTransform& InTransform, bool bPropagateToChildren = true)
	{
		if(InCachedElement.UpdateCache(this))
		{
			SetGlobalTransform(InCachedElement.GetKey().Type, InCachedElement.GetIndex(), InTransform, bPropagateToChildren);
		}
	}
	void SetGlobalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform, bool bPropagateToChildren = true);

	FRigElementKey GetParentKey(const FRigElementKey& InKey) const;

	TArray<FRigElementKey> GetChildKeys(const FRigElementKey& InKey, bool bRecursive = false) const;

	bool IsParentedTo(ERigElementType InChildType, int32 InChildIndex, ERigElementType InParentType, int32 InParentIndex) const;

	void SortKeyArray(TArray<FRigElementKey>& InOutKeysToSort) const;

#if WITH_EDITOR

	FString ExportSelectionToText() const;
	FString ExportToText(const TArray<FRigElementKey>& InSelection) const;
	TArray<FRigElementKey> ImportFromText(const FString& InContent, ERigHierarchyImportMode InImportMode = ERigHierarchyImportMode::Append, bool bSelectNewElements = true);

	TArray<FRigElementKey> DuplicateItems(const TArray<FRigElementKey>& InSelection, bool bSelectNewElements = true);
	TArray<FRigElementKey> MirrorItems(const TArray<FRigElementKey>& InSelection, const FRigMirrorSettings& InSettings, bool bSelectNewElements = true);


#endif

	TArray<FRigElementKey> CurrentSelection() const;
	TArray<FRigElementKey> GetAllItems(bool bSort = false) const;


	bool Select(const FRigElementKey& InKey, bool bSelect = true);
	bool ClearSelection();
	bool ClearSelection(ERigElementType InElementType);
	bool IsSelected(const FRigElementKey& InKey) const;

	FRigElementSelected OnElementSelected;
	FRigElementChanged OnElementChanged;

#if WITH_EDITOR
	FRigElementAdded OnElementAdded;
	FRigElementRemoved OnElementRemoved;
	FRigElementRenamed OnElementRenamed;
	FRigElementReparented OnElementReparented;

#endif

	void SendEvent(const FRigEventContext& InEvent, bool bAsyncronous = true);
	FRigEventDelegate OnEventReceived;

protected:

	void HandleOnElementSelected(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, bool bSelected);
#if WITH_EDITOR
	void HandleOnElementAdded(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey);
	void HandleOnElementRemoved(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey);
	void HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName);
	void HandleOnElementReparented(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName);
#endif

private:

	void UpdateDepthIndexIfRequired() const;
	mutable TMap<FRigElementKey, int32> DepthIndexByKey;

	void AppendToPose(FRigPose& InOutPose) const;

	friend class SRigHierarchy;
};

// this struct is still here for backwards compatibility - but not used anywhere
USTRUCT()
struct CONTROLRIG_API FRigHierarchyRef
{
	GENERATED_BODY()
};
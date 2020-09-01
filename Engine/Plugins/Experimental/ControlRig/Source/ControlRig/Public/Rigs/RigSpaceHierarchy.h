// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigHierarchyPose.h"
#include "RigSpaceHierarchy.generated.h"

class UControlRig;

UENUM(BlueprintType)
enum class ERigSpaceType : uint8
{
	/** Not attached to anything */
	Global,

	/** Attached to a bone */
	Bone,

	/** Attached to a control */
	Control,

	/** Attached to a space*/
	Space
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigSpace : public FRigElement
{
	GENERATED_BODY()

	FRigSpace()
		: FRigElement()
		, SpaceType(ERigSpaceType::Global)
		, ParentName(NAME_None)
		, ParentIndex(INDEX_NONE)
		, InitialTransform(FTransform::Identity)
		, LocalTransform(FTransform::Identity)
	{
	}
	virtual ~FRigSpace() {}

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = FRigElement)
	ERigSpaceType SpaceType;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = FRigElement)
	FName ParentName;

	UPROPERTY(BlueprintReadOnly, transient, Category = FRigElement)
	int32 ParentIndex;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = FRigElement)
	FTransform InitialTransform;

	UPROPERTY(BlueprintReadOnly, transient, EditAnywhere, Category = FRigElement)
	FTransform LocalTransform;

	FORCEINLINE virtual ERigElementType GetElementType() const override
	{
		return ERigElementType::Space;
	}

	FORCEINLINE virtual FRigElementKey GetParentElementKey(bool bForce = false) const
	{
		if (ParentIndex != INDEX_NONE || bForce)
		{
			switch (SpaceType)
			{
				case ERigSpaceType::Bone:
				{
					return FRigElementKey(ParentName, ERigElementType::Bone);
				}
				case ERigSpaceType::Control:
				{
					return FRigElementKey(ParentName, ERigElementType::Control);
				}
				case ERigSpaceType::Space:
				{
					return FRigElementKey(ParentName, ERigElementType::Space);
				}
				default:
				{
					break;
				}
			}
		}
		return FRigElementKey();
	}
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigSpaceHierarchy
{
	GENERATED_BODY()

	FRigSpaceHierarchy();
	FRigSpaceHierarchy& operator= (const FRigSpaceHierarchy &InOther);

	FORCEINLINE ERigElementType RigElementType() const { return ERigElementType::Space; }

	FORCEINLINE int32 Num() const { return Spaces.Num(); }
	FORCEINLINE const FRigSpace& operator[](int32 InIndex) const { return Spaces[InIndex]; }
	FORCEINLINE FRigSpace& operator[](int32 InIndex) { return Spaces[InIndex]; }
	FORCEINLINE const FRigSpace& operator[](const FName& InName) const { return Spaces[GetIndex(InName)]; }
	FORCEINLINE FRigSpace& operator[](const FName& InName) { return Spaces[GetIndex(InName)]; }

	FORCEINLINE const TArray<FRigSpace>& GetSpaces() const { return Spaces; }

	FORCEINLINE TArray<FRigSpace>::RangedForIteratorType      begin()       { return Spaces.begin(); }
	FORCEINLINE TArray<FRigSpace>::RangedForConstIteratorType begin() const { return Spaces.begin(); }
	FORCEINLINE TArray<FRigSpace>::RangedForIteratorType      end()         { return Spaces.end();   }
	FORCEINLINE TArray<FRigSpace>::RangedForConstIteratorType end() const   { return Spaces.end();   }

	FORCEINLINE bool IsNameAvailable(const FName& InPotentialNewName) const { return GetIndex(InPotentialNewName) == INDEX_NONE; }

	FName GetSafeNewName(const FName& InPotentialNewName) const;

	FRigSpace& Add(const FName& InNewName, ERigSpaceType InSpaceType = ERigSpaceType::Global, const FName& InParentName = NAME_None, const FTransform& InTransform = FTransform::Identity);

	FRigSpace Remove(const FName& InNameToRemove);

	FName Rename(const FName& InOldName, const FName& InNewName);

	bool Reparent(const FName& InName, ERigSpaceType InSpaceType, const FName& InNewParentName);

	FName GetName(int32 InIndex) const;

	FORCEINLINE int32 GetIndex(const FName& InName) const
	{
		if(NameToIndexMapping.Num() != Spaces.Num())
		{
			return GetIndexSlow(InName);
		}

		const int32* Index = NameToIndexMapping.Find(InName);
		if (Index)
		{
			return *Index;
		}

		return INDEX_NONE;
	}

	void SetGlobalTransform(const FName& InName, const FTransform& InTransform);

	void SetGlobalTransform(int32 InIndex, const FTransform& InTransform);

	FTransform GetGlobalTransform(const FName& InName) const;

	FTransform GetGlobalTransform(int32 InIndex) const;

	void SetLocalTransform(const FName& InName, const FTransform& InTransform);

	void SetLocalTransform(int32 InIndex, const FTransform& InTransform);

	FTransform GetLocalTransform(const FName& InName) const;

	FTransform GetLocalTransform(int32 InIndex) const;

	void SetInitialTransform(const FName& InName, const FTransform& InTransform);

	void SetInitialTransform(int32 InIndex, const FTransform& InTransform);

	FTransform GetInitialTransform(const FName& InName) const;

	FTransform GetInitialTransform(int32 InIndex) const;

	void SetInitialGlobalTransform(const FName& InName, const FTransform& GlobalTransform);
	
	void SetInitialGlobalTransform(int32 InIndex, const FTransform& GlobalTransform);
	
	FTransform GetInitialGlobalTransform(const FName& InName) const;
	
	FTransform GetInitialGlobalTransform(int32 InIndex) const;

	// updates all of the internal caches
	void Initialize(bool bResetTransforms = true);

	// clears the hierarchy and removes all content
	void Reset();

	// returns the current pose
	FRigPose GetPose() const;

	// sets the current transforms from the given pose
	void SetPose(FRigPose& InPose);

	// resets all of the transforms back to the initial transform
	void ResetTransforms();

	// copies all initial transforms from another hierarchy
	void CopyInitialTransforms(const FRigSpaceHierarchy& InOther);


	bool Select(const FName& InName, bool bSelect = true);
	bool ClearSelection();
	TArray<FName> CurrentSelection() const;
	bool IsSelected(const FName& InName) const;

	FRigElementSelected OnSpaceSelected;

#if WITH_EDITOR
	FRigElementAdded OnSpaceAdded;
	FRigElementRemoved OnSpaceRemoved;
	FRigElementRenamed OnSpaceRenamed;
	FRigElementReparented OnSpaceReparented;

	void HandleOnElementRemoved(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey);
	void HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName);

#endif

private:

	// disable copy constructor
	FRigSpaceHierarchy(const FRigSpaceHierarchy& InOther) {}

	FRigHierarchyContainer* Container;

	int32 GetParentIndex(ERigSpaceType InSpaceType, const FName& InName) const;

	UPROPERTY(EditAnywhere, Category = FRigSpaceHierarchy)
	TArray<FRigSpace> Spaces;

	// can serialize fine? 
	UPROPERTY()
	TMap<FName, int32> NameToIndexMapping;

	UPROPERTY(transient)
	TArray<FName> Selection;

	int32 GetIndexSlow(const FName& InName) const;

	void RefreshMapping();

	void AppendToPose(FRigPose& InOutPose) const;

#if WITH_EDITOR
	mutable TArray<bool> RecursionGuard;
#endif

	friend struct FRigHierarchyContainer;
	friend struct FCachedRigElement;
	friend class UControlRigHierarchyModifier;
	friend class UControlRig;
};

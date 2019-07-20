// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigControlHierarchy.generated.h"

class UControlRig;

UENUM(BlueprintType)
enum class ERigControlType : uint8
{
	Bool,
	Float,
	Vector2D,
	Vector,
	Quat,
	Rotator,
	Transform
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigControl
{
	GENERATED_BODY()

	FRigControl()
		: Name(NAME_None)
		, Index(INDEX_NONE)
		, ControlType(ERigControlType::Transform)
		, ParentName(NAME_None)
		, ParentIndex(INDEX_NONE)
		, SpaceName(NAME_None)
		, SpaceIndex(INDEX_NONE)
		, InitialTransform(FTransform::Identity)
		, LocalTransform(FTransform::Identity)
		, Color(FLinearColor::Black)
		, Dependents()
	{
	}

	UPROPERTY(VisibleAnywhere, Category = FRigControlHierarchy)
	FName Name;

	UPROPERTY(VisibleAnywhere, Category = FRigControlHierarchy)
	int32 Index;

	UPROPERTY(VisibleAnywhere, Category = FRigControlHierarchy)
	ERigControlType ControlType;

	UPROPERTY(VisibleAnywhere, Category = FRigControlHierarchy)
	FName ParentName;

	UPROPERTY(transient)
	int32 ParentIndex;

	UPROPERTY(VisibleAnywhere, Category = FRigControlHierarchy)
	FName SpaceName;

	UPROPERTY(transient)
	int32 SpaceIndex;

	UPROPERTY(VisibleAnywhere, Category = FRigControlHierarchy)
	FTransform InitialTransform;

	UPROPERTY(transient, VisibleAnywhere, Category = FRigControlHierarchy)
	FTransform LocalTransform;

	UPROPERTY(VisibleAnywhere, Category = FRigControlHierarchy)
	FLinearColor Color;

	/** dependent list - direct dependent for child or anything that needs to update due to this */
	UPROPERTY(transient)
	TArray<int32> Dependents;
};

USTRUCT()
struct CONTROLRIG_API FRigControlHierarchy
{
	GENERATED_BODY()

	FRigControlHierarchy();
	FRigControlHierarchy& operator= (const FRigControlHierarchy &InOther);

	FORCEINLINE ERigElementType RigElementType() const { return ERigElementType::Control; }

	FORCEINLINE int32 Num() const { return Controls.Num(); }
	FORCEINLINE const FRigControl& operator[](int32 InIndex) const { return Controls[InIndex]; }
	FORCEINLINE FRigControl& operator[](int32 InIndex) { return Controls[InIndex]; }
	FORCEINLINE const FRigControl& operator[](const FName& InName) const { return Controls[GetIndex(InName)]; }
	FORCEINLINE FRigControl& operator[](const FName& InName) { return Controls[GetIndex(InName)]; }

	FORCEINLINE TArray<FRigControl>::RangedForIteratorType      begin()       { return Controls.begin(); }
	FORCEINLINE TArray<FRigControl>::RangedForConstIteratorType begin() const { return Controls.begin(); }
	FORCEINLINE TArray<FRigControl>::RangedForIteratorType      end()         { return Controls.end();   }
	FORCEINLINE TArray<FRigControl>::RangedForConstIteratorType end() const   { return Controls.end();   }

	FORCEINLINE bool IsNameAvailable(const FName& InPotentialNewName) const { return GetIndex(InPotentialNewName) == INDEX_NONE; }

	FName GetSafeNewName(const FName& InPotentialNewName) const;

	FRigControl& Add(const FName& InNewName, ERigControlType InControlType = ERigControlType::Transform, const FName& InParentName = NAME_None, const FName& InSpaceName = NAME_None, const FTransform& InTransform = FTransform::Identity);

	FRigControl Remove(const FName& InNameToRemove);

	FName Rename(const FName& InOldName, const FName& InNewName);

	bool Reparent(const FName& InName, const FName& InNewParentName);

	void SetSpace(const FName& InName, const FName& InNewSpaceName);

	FName GetName(int32 InIndex) const;

	FORCEINLINE int32 GetIndex(const FName& InName) const
	{
		if(NameToIndexMapping.Num() != Controls.Num())
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

	// list of names of children - this is not cheap, and is supposed to be used only for one time set up
	int32 GetChildren(const FName& InName, TArray<int32>& OutChildren, bool bRecursively) const;

	int32 GetChildren(const int32 InIndex, TArray<int32>& OutChildren, bool bRecursively) const;

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

	// updates all of the internal caches
	void Initialize();

	// clears the hierarchy and removes all content
	void Reset();

	// resets all of the transforms back to the initial transform
	void ResetTransforms();

#if WITH_EDITOR

	bool Select(const FName& InName, bool bSelect = true);
	bool ClearSelection();
	TArray<FName> CurrentSelection() const;
	bool IsSelected(const FName& InName) const;

	FRigElementAdded OnControlAdded;
	FRigElementRemoved OnControlRemoved;
	FRigElementRenamed OnControlRenamed;
	FRigElementReparented OnControlReparented;
	FRigElementSelected OnControlSelected;

	void HandleOnElementRemoved(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InName);
	void HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName);

#endif

private:

	// disable copy constructor
	FRigControlHierarchy(const FRigControlHierarchy& InOther) {}

	FRigHierarchyContainer* Container;

	UPROPERTY(EditAnywhere, Category = FRigControlHierarchy)
	TArray<FRigControl> Controls;

	// can serialize fine? 
	UPROPERTY()
	TMap<FName, int32> NameToIndexMapping;

#if WITH_EDITORONLY_DATA
	UPROPERTY(transient)
	TArray<FName> Selection;
#endif

	int32 GetSpaceIndex(const FName& InName) const;

	int32 GetIndexSlow(const FName& InName) const;

	void RefreshMapping();
	void Sort();

	// list of names of children - this is not cheap, and is supposed to be used only for one time set up
	int32 GetChildrenRecursive(const int32 InIndex, TArray<int32>& OutChildren, bool bRecursively) const;

	friend struct FRigHierarchyContainer;
};

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigSpaceHierarchy.generated.h"

class UControlRig;

UENUM(BlueprintType)
enum class ERigSpaceType : uint8
{
	/** Not attached to anything */
	Global,

	/** Attached to a bone */
	Bone,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigSpace
{
	GENERATED_BODY()

	FRigSpace()
		: Name(NAME_None)
		, Index(INDEX_NONE)
		, SpaceType(ERigSpaceType::Global)
		, ParentName(NAME_None)
		, ParentIndex(INDEX_NONE)
		, InitialTransform(FTransform::Identity)
		, LocalTransform(FTransform::Identity)
	{
	}

	UPROPERTY(VisibleAnywhere, Category = FRigSpaceHierarchy)
	FName Name;

	UPROPERTY(VisibleAnywhere, Category = FRigCurveContainer)
	int32 Index;

	UPROPERTY(VisibleAnywhere, Category = FRigSpaceHierarchy)
	ERigSpaceType SpaceType;

	UPROPERTY(VisibleAnywhere, Category = FRigSpaceHierarchy)
	FName ParentName;

	UPROPERTY(transient)
	int32 ParentIndex;

	UPROPERTY(VisibleAnywhere, Category = FRigSpaceHierarchy)
	FTransform InitialTransform;

	UPROPERTY(transient, VisibleAnywhere, Category = FRigSpaceHierarchy)
	FTransform LocalTransform;
};

USTRUCT()
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

	FORCEINLINE TArray<FRigSpace>::RangedForIteratorType      begin()       { return Spaces.begin(); }
	FORCEINLINE TArray<FRigSpace>::RangedForConstIteratorType begin() const { return Spaces.begin(); }
	FORCEINLINE TArray<FRigSpace>::RangedForIteratorType      end()         { return Spaces.end();   }
	FORCEINLINE TArray<FRigSpace>::RangedForConstIteratorType end() const   { return Spaces.end();   }

	FORCEINLINE bool IsNameAvailable(const FName& InPotentialNewName) const { return GetIndex(InPotentialNewName) == INDEX_NONE; }

	FName GetSafeNewName(const FName& InPotentialNewName) const;

	FRigSpace& Add(const FName& InNewName, ERigSpaceType InSpaceType, const FName& InParentName, const FTransform& InTransform);

	FRigSpace Remove(const FName& InNameToRemove);

	FName Rename(const FName& InOldName, const FName& InNewName);

	void Reparent(const FName& InName, const FName& InNewParentName);

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

	// updates all of the internal caches
	void Initialize();

	// clears the hierarchy and removes all content
	void Reset();

	// resets all of the transforms back to the initial transform
	void ResetTransforms();

private:

	// disable copy constructor
	FRigSpaceHierarchy(const FRigSpaceHierarchy& InOther) {}

#if WITH_EDITOR
	FRigHierarchyContainer* Container;
	FRigElementAdded OnSpaceAdded;
	FRigElementRemoved OnSpaceRemoved;
	FRigElementRenamed OnSpaceRenamed;
#endif

	UPROPERTY(EditAnywhere, Category = FRigSpaceHierarchy)
	TArray<FRigSpace> Spaces;

	// can serialize fine? 
	UPROPERTY()
	TMap<FName, int32> NameToIndexMapping;

	int32 GetIndexSlow(const FName& InName) const;

	void RefreshMapping();

	friend struct FRigHierarchyContainer;
};

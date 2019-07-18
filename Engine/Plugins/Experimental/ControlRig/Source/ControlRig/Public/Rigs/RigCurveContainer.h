// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/StatsHierarchical.h"
#include "RigHierarchyDefines.h"
#include "RigCurveContainer.generated.h"

class UControlRig;

USTRUCT(BlueprintType)
struct FRigCurve
{
	GENERATED_BODY()

	FRigCurve()
		: Name(NAME_None)
		, Index(INDEX_NONE)
		, Value(0.f)
	{
	}

	UPROPERTY(VisibleAnywhere, Category = FRigCurveContainer)
	FName Name;

	UPROPERTY(VisibleAnywhere, Category = FRigCurveContainer)
	int32 Index;

	UPROPERTY(VisibleAnywhere, Category = FRigCurveContainer)
	float Value;
};

USTRUCT()
struct CONTROLRIG_API FRigCurveContainer
{
	GENERATED_BODY()

public:

	FRigCurveContainer();
	FRigCurveContainer& operator= (const FRigCurveContainer &InOther);

	FORCEINLINE ERigElementType RigElementType() const { return ERigElementType::Curve; }

	FORCEINLINE int32 Num() const { return Curves.Num(); }
	FORCEINLINE const FRigCurve& operator[](int32 InIndex) const { return Curves[InIndex]; }
	FORCEINLINE FRigCurve& operator[](int32 InIndex) { return Curves[InIndex]; }
	FORCEINLINE const FRigCurve& operator[](const FName& InName) const { return Curves[GetIndex(InName)]; }
	FORCEINLINE FRigCurve& operator[](const FName& InName) { return Curves[GetIndex(InName)]; }

	FORCEINLINE TArray<FRigCurve>::RangedForIteratorType      begin()       { return Curves.begin(); }
	FORCEINLINE TArray<FRigCurve>::RangedForConstIteratorType begin() const { return Curves.begin(); }
	FORCEINLINE TArray<FRigCurve>::RangedForIteratorType      end()         { return Curves.end();   }
	FORCEINLINE TArray<FRigCurve>::RangedForConstIteratorType end() const   { return Curves.end();   }

	FORCEINLINE bool IsNameAvailable(const FName& InPotentialNewName) const { return GetIndex(InPotentialNewName) == INDEX_NONE; }

	FName GetSafeNewName(const FName& InPotentialNewName) const;

	FRigCurve& Add(const FName& InNewName);

	FRigCurve Remove(const FName& InName);

	FName GetName(int32 InIndex) const;

	FORCEINLINE int32 GetIndex(const FName& InName) const
	{
		if(NameToIndexMapping.Num() != Curves.Num())
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

	void SetValue(const FName& InName, const float InValue);

	void SetValue(int32 InIndex, const float InValue);

	float GetValue(const FName& InName) const;

	float GetValue(int32 InIndex) const;

	FName Rename(const FName& InOldName, const FName& InNewName);

	// updates all of the internal caches
	void Initialize();

	// clears the CurveContainer and removes all content
	void Reset();

	// resets all of the transforms back to the initial transform
	void ResetValues();

#if WITH_EDITOR
	FRigHierarchyContainer* Container;
	FRigElementAdded OnCurveAdded;
	FRigElementRemoved OnCurveRemoved;
	FRigElementRenamed OnCurveRenamed;
#endif

private:

	// disable copy constructor
	FRigCurveContainer(const FRigCurveContainer& InOther) {}

	UPROPERTY(EditAnywhere, Category = FRigCurveContainer)
	TArray<FRigCurve> Curves;

	UPROPERTY()
	TMap<FName, int32> NameToIndexMapping;

	int32 GetIndexSlow(const FName& InName) const;

	void RefreshMapping();
};

// todo
//USTRUCT()
//struct CONTROLRIG_API FRigHierarchyRef
//{
//	GENERATED_BODY()
//};
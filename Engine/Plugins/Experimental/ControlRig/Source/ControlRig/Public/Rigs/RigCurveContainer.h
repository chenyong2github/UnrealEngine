// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/StatsHierarchical.h"
#include "RigHierarchyDefines.h"
#include "RigHierarchyPose.h"
#include "RigCurveContainer.generated.h"

class UControlRig;
class USkeleton;

USTRUCT(BlueprintType)
struct FRigCurve : public FRigElement
{
	GENERATED_BODY()

	FRigCurve()
		: FRigElement()
		, Value(0.f)
	{
	}
	virtual ~FRigCurve() {}

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = FRigElement)
	float Value;

	FORCEINLINE virtual ERigElementType GetElementType() const override
	{
		return ERigElementType::Curve;
	}
};

USTRUCT(BlueprintType)
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

	FRigCurve& Add(const FName& InNewName, float InValue = 0.f);

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

	// returns the current pose
	FRigPose GetPose() const;

	// sets the current values from the given pose
	void SetPose(FRigPose& InPose);

	FRigHierarchyContainer* Container;

	bool Select(const FName& InName, bool bSelect = true);
	bool ClearSelection();
	TArray<FName> CurrentSelection() const;
	bool IsSelected(const FName& InName) const;

	FRigElementSelected OnCurveSelected;

#if WITH_EDITOR

	FRigElementAdded OnCurveAdded;
	FRigElementRemoved OnCurveRemoved;
	FRigElementRenamed OnCurveRenamed;

	TArray<FRigElementKey> ImportCurvesFromSkeleton(const USkeleton* InSkeleton, const FName& InNameSpace, bool bRemoveObsoleteCurves, bool bSelectCurves, bool bNotify);

#endif

private:

	// disable copy constructor
	FRigCurveContainer(const FRigCurveContainer& InOther) {}

	UPROPERTY(EditAnywhere, Category = FRigCurveContainer)
	TArray<FRigCurve> Curves;

	UPROPERTY()
	TMap<FName, int32> NameToIndexMapping;

	UPROPERTY(transient)
	TArray<FName> Selection;

	int32 GetIndexSlow(const FName& InName) const;

	void RefreshMapping();

	void AppendToPose(FRigPose& InOutPose) const;

	bool bSuspendNotifications;

	friend struct FRigHierarchyContainer;
	friend struct FCachedRigElement;
	friend class UControlRigHierarchyModifier;
};


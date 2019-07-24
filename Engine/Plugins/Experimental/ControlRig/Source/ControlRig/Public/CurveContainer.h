// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/StatsHierarchical.h"
#include "CurveContainer.generated.h"

class UControlRig;

USTRUCT(BlueprintType)
struct FRigCurve
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = FRigCurveContainer)
	FName Name;

	UPROPERTY(VisibleAnywhere, Category = FRigCurveContainer)
	float Value;
};

USTRUCT()
struct CONTROLRIG_API FRigCurveContainer
{
	GENERATED_BODY()

private:
	UPROPERTY(EditAnywhere, Category = FRigCurveContainer)
	TArray<FRigCurve> Curves;

	// can serialize fine? 
	UPROPERTY()
	TMap<FName, int32> NameToIndexMapping;

public:

	const TArray<FRigCurve>& GetCurves() const
	{
		return Curves;
	}

	void AddCurve(const FName& NewCurveName)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		int32 Found = GetIndex(NewCurveName);
		if (Found == INDEX_NONE)
		{
			FRigCurve NewCurve;
			NewCurve.Name = NewCurveName;
			NewCurve.Value = 0.f;
			Curves.Add(NewCurve);
			RefreshMapping();
		}
	}

	void DeleteCurve(const FName& CurveToDelete)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		int32 IndexToDelete = GetIndex(CurveToDelete);
		if (IndexToDelete != INDEX_NONE)
		{
			Curves.RemoveAt(IndexToDelete);
			RefreshMapping();
		}
	}

	FName GetName(int32 Index) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (Curves.IsValidIndex(Index))
		{
			return Curves[Index].Name;
		}

		return NAME_None;
	}

	int32 GetIndex(const FName& Curve) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		const int32* Index = NameToIndexMapping.Find(Curve);
		if (Index)
		{
			return *Index;
		}

		return INDEX_NONE;
	}

//#if WITH_EDITOR
	// @FIXMELINA: figure out how to remove this outside of editor
	// ignore mapping, run slow search
	// this is useful in editor while editing
	// we don't want to build mapping data every time
	int32 GetIndexSlow(const FName& Curve) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		for (int32 CurveId = 0; CurveId < Curves.Num(); ++CurveId)
		{
			if (Curves[CurveId].Name == Curve)
			{
				return CurveId;
			}
		}

		return INDEX_NONE;
	}
//#endif // WITH_EDITOR
	void SetValue(const FName& Curve, const float Value)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		SetValue(GetIndex(Curve), Value);
	}

	void SetValue(int32 Index, const float Value)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (Curves.IsValidIndex(Index))
		{
			FRigCurve& Curve = Curves[Index];
			Curve.Value = Value;
		}
	}

	float GetValue(const FName& Curve) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		return GetValue(GetIndex(Curve));
	}

	float GetValue(int32 Index) const
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (Curves.IsValidIndex(Index))
		{
			return Curves[Index].Value;
		}

		return 0.f;
	}

	void Rename(const FName& OldName, const FName& NewName)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		if (OldName != NewName)
		{
			const int32 Found = GetIndex(OldName);
			const int32 NewNameFound = GetIndex(NewName);
			// if I have new name, and didn't find new name
			if (Found != INDEX_NONE && NewNameFound == INDEX_NONE)
			{
				Curves[Found].Name = NewName;
				RefreshMapping();
			}
		}
	}

	// updates all of the internal caches
	void Initialize();

	// clears the CurveContainer and removes all content
	void Reset();

	// resets all of the transforms back to the initial transform
	void ResetValues();

	int32 GetNum() const
	{
		return Curves.Num();
	}

private:
	void RefreshMapping();
};


// @todo: do we need this container now?
USTRUCT()
struct FRigCurveContainerRef
{
	GENERATED_BODY()

	FRigCurveContainerRef(FRigCurveContainer* InContainer = nullptr)
		: Container(InContainer)
	{

	}

	FRigCurveContainer* Get() 
	{
		return GetInternal();
	}

	const FRigCurveContainer* Get() const
	{
		return (const FRigCurveContainer*)GetInternal();
	}

private:
	struct FRigCurveContainer* Container;

	FRigCurveContainer* GetInternal() const
	{
		return Container;
	}

	friend class UControlRig;
	friend class FControlRigUnitTestBase;
};
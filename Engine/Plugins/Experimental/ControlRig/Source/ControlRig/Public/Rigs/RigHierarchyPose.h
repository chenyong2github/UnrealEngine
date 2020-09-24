// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigHierarchyCache.h"
#include "RigHierarchyPose.generated.h"

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigPoseElement
{
public:

	GENERATED_BODY()

	FRigPoseElement()
	: Index()
	, GlobalTransform(FTransform::Identity)
	, LocalTransform(FTransform::Identity)
	, CurveValue(0.f)
	{
	}

	UPROPERTY()
	FCachedRigElement Index;

	UPROPERTY()
	FTransform GlobalTransform;

	UPROPERTY()
	FTransform LocalTransform;

	UPROPERTY()
	float CurveValue;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigPose
{
public:

	GENERATED_BODY()

	FORCEINLINE void Reset() { Elements.Reset(); }

	FORCEINLINE int32 Num() const { return Elements.Num(); }
	FORCEINLINE const FRigPoseElement& operator[](int32 InIndex) const { return Elements[InIndex]; }
	FORCEINLINE FRigPoseElement& operator[](int32 InIndex) { return Elements[InIndex]; }

	FORCEINLINE TArray<FRigPoseElement>::RangedForIteratorType      begin()       { return Elements.begin(); }
	FORCEINLINE TArray<FRigPoseElement>::RangedForConstIteratorType begin() const { return Elements.begin(); }
	FORCEINLINE TArray<FRigPoseElement>::RangedForIteratorType      end()         { return Elements.end();   }
	FORCEINLINE TArray<FRigPoseElement>::RangedForConstIteratorType end() const   { return Elements.end();   }

	UPROPERTY()
	TArray<FRigPoseElement> Elements;
};

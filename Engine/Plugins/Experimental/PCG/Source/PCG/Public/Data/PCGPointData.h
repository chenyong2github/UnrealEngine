// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSpatialData.h"

#include "Math/GenericOctreePublic.h"
#include "Math/GenericOctree.h"

#include "PCGPointData.generated.h"

class AActor;

struct FPCGPointRef
{
	FPCGPointRef(const FPCGPoint& InPoint);
	FPCGPointRef(const FPCGPointRef& InPointRef);

	const FPCGPoint* Point;
	FBoxSphereBounds Bounds;
};

struct FPCGPointRefSemantics
{
	enum { MaxElementsPerLeaf = 16 };
	enum { MinInclusiveElementsPerNode = 7 };
	enum { MaxNodeDepth = 12 };

	typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

	FORCEINLINE static const FBoxSphereBounds& GetBoundingBox(const FPCGPointRef& InPoint)
	{
		return InPoint.Bounds;
	}

	FORCEINLINE static const bool AreElementsEqual(const FPCGPointRef& A, const FPCGPointRef& B)
	{
		// TODO: verify if that's sufficient
		return A.Point == B.Point;
	}

	FORCEINLINE static void ApplyOffset(FPCGPointRef& InPoint)
	{
		ensureMsgf(false, TEXT("Not implemented"));
	}

	FORCEINLINE static void SetElementId(const FPCGPointRef& Element, FOctreeElementId2 OctreeElementID)
	{
	}
};

// TODO: Split this in "concrete" vs "api" class (needed for views)
UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGPointData : public UPCGSpatialData
{
	GENERATED_BODY()

public:
	typedef TOctree2<FPCGPointRef, FPCGPointRefSemantics> PointOctree;

	void Initialize(AActor* InActor);

	// ~Begin UPCGSpatialData interface
	virtual int GetDimension() const override { return 0; }
	virtual FBox GetBounds() const override;
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	virtual const UPCGPointData* ToPointData() const { return this; }
	// ~End UPCGSpatialData interface

	UFUNCTION(BlueprintCallable, Category = SpatialData)
	const TArray<FPCGPoint>& GetPoints() const { return Points; }

	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void SetPoints(const TArray<FPCGPoint>& InPoints);
	
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void CopyPointsFrom(const UPCGPointData* InData, const TArray<int>& InDataIndices);

	const FPCGPoint* GetPointAtPosition(const FVector& InPosition) const;

	TArray<FPCGPoint>& GetMutablePoints();

	const PointOctree& GetOctree() const;

protected:
	void RebuildOctree() const;
	void RecomputeBounds() const;

	UPROPERTY()
	TArray<FPCGPoint> Points;

	mutable FCriticalSection CachedDataLock;
	mutable PointOctree Octree;
	mutable FBox Bounds; // TODO: review if this needs to be threadsafe
	mutable bool bBoundsAreDirty = true;
	mutable bool bOctreeIsDirty = true;
};
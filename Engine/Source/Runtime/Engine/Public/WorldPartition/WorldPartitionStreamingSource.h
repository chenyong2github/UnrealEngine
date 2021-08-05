// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"
#include "Math/RandomStream.h"
#include "WorldPartitionStreamingSource.generated.h"

/** See https://en.wikipedia.org/wiki/Spherical_sector. */
class FSphericalSector
{
public:
	using FReal = FVector::FReal;

	/** Creates and initializes a new spherical sector. */
	FSphericalSector(EForceInit)
		: Center(ForceInit)
		, Radius(0.0f)
		, Axis(ForceInit)
	{
		SetAsSphere();
	}

	/** Creates and initializes a spherical sector using given parameters. */
	FSphericalSector(FVector InCenter, FReal InRadius, FVector InAxis = FVector::ForwardVector, FReal InAngle = 0)
		: Center(InCenter)
		, Radius(InRadius)
	{
		SetAngle(InAngle);
		SetAxis(InAxis);
	}

	void SetCenter(const FVector& InCenter) { Center = InCenter; }
	const FVector& GetCenter() const { return Center; }

	void SetRadius(FReal InRadius) { Radius = InRadius; }
	FReal GetRadius() const { return Radius; }

	void SetAngle(FReal InAngle) { Angle = (InAngle <= 0.0f || InAngle > 360.0f) ? 360.0f : InAngle; }
	FReal GetAngle() const { return Angle; }

	void SetAxis(const FVector& InAxis) { Axis = InAxis.GetSafeNormal(); }
	FVector GetAxis() const { return Axis; }
	FVector GetScaledAxis() const { return Axis * Radius; }

	void SetAsSphere() { SetAngle(360.0f); }
	bool IsSphere() const { return FMath::IsNearlyEqual(Angle, 360.0f); }

	bool IsNearlyZero() const { return FMath::IsNearlyZero(Radius) || Axis.IsNearlyZero() || FMath::IsNearlyZero(Angle); }
	bool IsValid() const { return !IsNearlyZero(); }

	FBox CalcBounds() const
	{
		const FVector Offset(Radius);
		return FBox(Center - Offset, Center + Offset);
	}

	/** Get result of Transforming spherical sector with transform. */
	FSphericalSector TransformBy(const FTransform& M) const
	{
		FSphericalSector Result(M.TransformPosition(Center), M.GetMaximumAxisScale() * Radius, M.TransformVector(Axis), Angle);
		return Result;
	}

	/** Helper method that builds a list of debug display segments */
	TArray<TPair<FVector, FVector>> BuildDebugMesh() const
	{
		TArray<TPair<FVector, FVector>> Segments;
		if (!IsValid())
		{
			return Segments;
		}

		const int32 SegmentCount = FMath::Max(4, FMath::CeilToInt(64 * Angle / 360.f));
		const float AngleStep = Angle / float(SegmentCount);
		const FRotator ShapeRotation = FRotationMatrix::MakeFromX(Axis).Rotator();
		const FVector ScaledAxis = FVector::ForwardVector * Radius;

		FVector LastRollEndVector;
		for (int i = 0; i <= 64; ++i)
		{
			const float Roll = 360.0f * i / 64.0f;
			const FTransform Transform(FRotator(0, 0, Roll) + ShapeRotation, Center);
			FVector SegmentStart = Transform.TransformPosition(FRotator(0, -0.5f * Angle, 0).RotateVector(ScaledAxis));
			Segments.Emplace(Center, SegmentStart);
			for (int32 j = 1; j <= SegmentCount; j++)
			{
				FVector SegmentEnd = Transform.TransformPosition(FRotator(0, -0.5f * Angle + (AngleStep * j), 0).RotateVector(ScaledAxis));
				Segments.Emplace(SegmentStart, SegmentEnd);
				SegmentStart = SegmentEnd;
			}
			Segments.Emplace(Center, SegmentStart);
			if (i > 0)
			{
				Segments.Emplace(SegmentStart, LastRollEndVector);
			}
			LastRollEndVector = SegmentStart;
		}
		return Segments;
	}

private:
	/** Sphere center point. */
	FVector Center;

	/** Sphere radius. */
	FReal Radius;

	/** Sector axis (direction). */
	FVector Axis;

	/** Optional sector angle in degree (360 = regular sphere). */
	FReal Angle;
};

USTRUCT(BlueprintType)
struct FStreamingSourceShape
{
	GENERATED_BODY()

	FStreamingSourceShape()
	: bUseGridLoadingRange(true)
	, Radius(10000.0f)
	, bIsSector(false)
	, SectorAngle(360.0f)
	, Location(ForceInitToZero)
	, Rotation(ForceInitToZero)
	{}

	/* If True, streaming source shape radius is bound to loading range radius. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Streaming)
	bool bUseGridLoadingRange;

	/* Custom streaming source shape radius (not used if bUseGridLoadingRange is True). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Streaming, meta = (EditCondition = "!bUseGridLoadingRange"))
	float Radius;

	/* Whether the source shape is a spherical sector instead of a regular sphere source. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Streaming)
	bool bIsSector;

	/* Shape's spherical sector angle in degree (not used if bIsSector is False). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Streaming, meta = (EditCondition = "bIsSector", ClampMin = 0, ClampMax = 360))
	float SectorAngle;

	/* Streaming source shape location (local to streaming source). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Streaming)
	FVector Location;

	/* Streaming source shape rotation (local to streaming source). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Streaming)
	FRotator Rotation;
};

/** Helper class used to iterate over streaming source shapes. */
class FStreamingSourceShapeHelper
{
public:

	FORCEINLINE static void ForEachShape(float InGridLoadingRange, float InDefaultRadius, bool bInProjectIn2D, const FVector& InLocation, const FRotator& InRotation, const TArray<FStreamingSourceShape>& InShapes, TFunctionRef<void(const FSphericalSector&)> InOperation)
	{
		const FTransform Transform(bInProjectIn2D ? FRotator(0, InRotation.Yaw, 0) : InRotation, InLocation);
		if (InShapes.IsEmpty())
		{
			FSphericalSector LocalShape(FVector::ZeroVector, InDefaultRadius);
			if (LocalShape.IsValid())
			{
				InOperation(LocalShape.TransformBy(Transform));
			}
		}
		else
		{
			for (const FStreamingSourceShape& Shape : InShapes)
			{
				const FVector::FReal ShapeRadius = Shape.bUseGridLoadingRange ? InGridLoadingRange : Shape.Radius;
				const FVector::FReal ShapeAngle = Shape.bIsSector ? Shape.SectorAngle : 360.0f;
				const FVector ShapeAxis = bInProjectIn2D ? FRotator(0, Shape.Rotation.Yaw, 0).Vector() : Shape.Rotation.Vector();
				FSphericalSector LocalShape(bInProjectIn2D ? FVector(Shape.Location.X, Shape.Location.Y, 0) : Shape.Location, ShapeRadius, ShapeAxis, ShapeAngle);
				if (LocalShape.IsValid())
				{
					InOperation(LocalShape.TransformBy(Transform));
				}
			}
		}
	}
};

/**
 * Streaming Source Target State
 */
UENUM()
enum class EStreamingSourceTargetState : uint8
{
	Loaded,
	Activated
};

/**
 * Structure containing all properties required to query a streaming state
 */
USTRUCT(BlueprintType)
struct FWorldPartitionStreamingQuerySource
{
	GENERATED_USTRUCT_BODY()

	FWorldPartitionStreamingQuerySource()
		: Location(FVector::ZeroVector)
		, Radius(0.f)
		, bUseGridLoadingRange(true)
		, bDataLayersOnly(false)
		, bSpatialQuery(true)
		, Rotation(ForceInitToZero)
		, TargetGrid(NAME_None)
	{}

	FWorldPartitionStreamingQuerySource(const FVector& InLocation)
		: Location(InLocation)
		, Radius(0.f)
		, bUseGridLoadingRange(true)
		, bDataLayersOnly(false)
		, bSpatialQuery(true)
		, Rotation(ForceInitToZero)
		, TargetGrid(NAME_None)
	{}

	/* Location to query. (not used if bSpatialQuery is false) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	FVector Location;

	/* Radius to query. (not used if bSpatialQuery is false) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	float Radius;

	/* If True, Instead of providing a query radius, query can be bound to loading range radius. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	bool bUseGridLoadingRange;

	/* Optional list of data layers to specialize the query. If empty only non data layer cells will be returned by the query. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	TArray<FName> DataLayers;

	/* If True, Only cells that are in a data layer found in DataLayers property will be returned by the query. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	bool bDataLayersOnly;

	/* If False, Location/Radius will not be used to find the cells. Only AlwaysLoaded cells will be returned by the query. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	bool bSpatialQuery;

	/* Reserved settings used by UWorldPartitionStreamingSourceComponent::IsStreamingCompleted. */
	FRotator Rotation;
	FName TargetGrid;
	TArray<FStreamingSourceShape> Shapes;

	/** Helper method that iterates over all shapes. If none is provided, it will still pass a sphere shape using Radius or grid's loading range (see bUseGridLoadingRange). */
	FORCEINLINE void ForEachShape(float InGridLoadingRange, FName InGridName, bool bInProjectIn2D, TFunctionRef<void(const FSphericalSector&)> InOperation) const
	{
		if (bSpatialQuery && (TargetGrid.IsNone() || TargetGrid == InGridName))
		{
			FStreamingSourceShapeHelper::ForEachShape(InGridLoadingRange, bUseGridLoadingRange ? InGridLoadingRange : Radius, bInProjectIn2D, Location, Rotation, Shapes, InOperation);
		}
	}
};

/**
 * Streaming Source Priority
 */
enum class EStreamingSourcePriority : int32
{
	Highest = INT_MIN,
	High = -4096,
	Normal = 0,
	Low = 4096,
	Lowest = INT_MAX,
	Default = Normal
};

/**
 * Structure containing all properties required to stream from a source
 */
struct ENGINE_API FWorldPartitionStreamingSource
{
	FWorldPartitionStreamingSource()
		: bBlockOnSlowLoading(false)
		, Priority(EStreamingSourcePriority::Default)
		, Velocity(0.f)
		, TargetGrid(NAME_None)
	{}

	FWorldPartitionStreamingSource(FName InName, const FVector& InLocation, const FRotator& InRotation, EStreamingSourceTargetState InTargetState, bool bInBlockOnSlowLoading, EStreamingSourcePriority InPriority = EStreamingSourcePriority::Default, float InVelocity = 0.f)
		: Name(InName)
		, Location(InLocation)
		, Rotation(InRotation)
		, TargetState(InTargetState)
		, bBlockOnSlowLoading(bInBlockOnSlowLoading)
		, Priority(InPriority)
		, Velocity(InVelocity)
		, TargetGrid(NAME_None)
	{
	}

	FColor GetDebugColor() const { return FColor::MakeRedToGreenColorFromScalar(FRandomStream(Name).GetFraction()); }

	/** Source unique name. */
	FName Name;

	/** Source location. */
	FVector Location;
	
	/** Source orientation (can impact streaming cell prioritization). */
	FRotator Rotation;

	/** Target streaming state. */
	EStreamingSourceTargetState TargetState;

	/** Whether this source will be considered when world partition detects slow loading and waits for cell streaming to complete. */
	bool bBlockOnSlowLoading;

	/** Streaming source priority. */
	EStreamingSourcePriority Priority;

	/** Source velocity (computed automatically). */
	float Velocity;

	/** When set, will only affect streaming on the provided target runtime streaming grid. When none is provided, applies to all runtime streaming grid. */
	FName TargetGrid;

	/** Source internal shapes. When none are provided, a sphere is automatically used. It's radius is equal to grid's loading range and center equals source's location. */
	TArray<FStreamingSourceShape> Shapes;

	/** Returns a box encapsulating all shapes. */
	FORCEINLINE FBox CalcBounds(float InGridLoadingRange, FName InGridName, bool bCalcIn2D = false) const
	{
		FBox OutBounds(ForceInit);
		ForEachShape(InGridLoadingRange, InGridName, bCalcIn2D, [&OutBounds](const FSphericalSector& Sector)
		{
			OutBounds += Sector.CalcBounds();
		});
		return OutBounds;
	}

	/** Helper method that iterates over all shapes. If none is provided, it will still pass a sphere shape using grid's loading range. */
	FORCEINLINE void ForEachShape(float InGridLoadingRange, FName InGridName, bool bInProjectIn2D, TFunctionRef<void(const FSphericalSector&)> InOperation) const
	{
		if (TargetGrid.IsNone() || TargetGrid == InGridName)
		{
			FStreamingSourceShapeHelper::ForEachShape(InGridLoadingRange, InGridLoadingRange, bInProjectIn2D, Location, Rotation, Shapes, InOperation);
		}
	}
};

/**
 * Interface for world partition streaming sources
 */
struct ENGINE_API IWorldPartitionStreamingSourceProvider
{
	virtual bool GetStreamingSource(FWorldPartitionStreamingSource& StreamingSource) = 0;
};
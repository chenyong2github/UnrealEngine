// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"
#include "Math/RandomStream.h"
#include "WorldPartitionStreamingSource.generated.h"

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
		, bUseLoadingRangeRadius(true)
		, bDataLayersOnly(false)
		, bSpatialQuery(true)
	{}

	FWorldPartitionStreamingQuerySource(const FVector& InLocation)
		: Location(InLocation)
		, Radius(0.f)
		, bUseLoadingRangeRadius(true)
		, bDataLayersOnly(false)
		, bSpatialQuery(true)
	{}

	/* Location to query. (not used if bSpatialQuery is false) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	FVector Location;

	/* Radius to query. (not used if bSpatialQuery is false) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	float Radius;

	/* If True, Instead of providing a query radius, query can be bound to loading range radius. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	bool bUseLoadingRangeRadius;

	/* Optional list of data layers to specialize the query. If empty only non data layer cells will be returned by the query. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	TArray<FName> DataLayers;

	/* If True, Only cells that are in a data layer found in DataLayers property will be returned by the query. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	bool bDataLayersOnly;

	/* If False, Location/Radius will not be used to find the cells. Only AlwaysLoaded cells will be returned by the query. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Query")
	bool bSpatialQuery;
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
		: Priority(EStreamingSourcePriority::Default)
	{}

	FWorldPartitionStreamingSource(FName InName, const FVector& InLocation, const FRotator& InRotation, EStreamingSourceTargetState InTargetState, EStreamingSourcePriority InPriority = EStreamingSourcePriority::Default)
		: Name(InName)
		, Location(InLocation)
		, Rotation(InRotation)
		, TargetState(InTargetState)
		, Priority(InPriority)
	{
	}

	FColor GetDebugColor() const { return FColor::MakeRedToGreenColorFromScalar(FRandomStream(Name).GetFraction()); }

	FName Name;
	FVector Location;
	FRotator Rotation;
	EStreamingSourceTargetState TargetState;
	EStreamingSourcePriority Priority;
};

/**
 * Interface for world partition streaming sources
 */
struct ENGINE_API IWorldPartitionStreamingSourceProvider
{
	virtual bool GetStreamingSource(FWorldPartitionStreamingSource& StreamingSource) = 0;
};
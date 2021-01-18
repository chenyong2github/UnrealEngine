// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapPlanesTypes.generated.h"

/** Control flags for plane queries. */
UENUM(BlueprintType)
enum class EMagicLeapPlaneQueryFlags : uint8
{
	/** Include planes whose normal is perpendicular to gravity. */
	Vertical,

	/** Include planes whose normal is parallel to gravity. */
	Horizontal,

	/** Include planes with arbitrary normals. */
	Arbitrary,

	/** If set, non-horizontal planes will be aligned perpendicular to gravity. */
	OrientToGravity,

	/** If set, inner planes will be returned; if not set, outer planes will be returned. */
	PreferInner,

	/** If set, include planes semantically tagged as ceiling. */
	Ceiling,

	/** If set, include planes semantically tagged as floor. */
	Floor,

	/** If set, include planes semantically tagged as wall. */
	Wall,

	/** Include polygons */
	Polygons
};

/** Persistent query types.
 */
UENUM(BlueprintType)
enum class EMagicLeapPlaneQueryType : uint8
{	
	/** All planes will be returned every query.*/
	Bulk,
	
	/** Planes will be returned as an array of new and removed planes in relation to the previous request. */
	Delta
};

/** Represents a plane returned from the ML-API. */
USTRUCT(BlueprintType)
struct MAGICLEAPPLANES_API FMagicLeapPlaneResult
{
	GENERATED_BODY()

public:
	/** Position of the center of the plane in world coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FVector PlanePosition = FVector(0.0f);

	/** Orientation of the plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FRotator PlaneOrientation = FRotator(0.0f);

	/** Orientation of the content with its up-vector orthogonal to the plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FRotator ContentOrientation = FRotator(0.0f);

	/** Width and height of the plane (in Unreal units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FVector2D PlaneDimensions = FVector2D(0.0f);

	/** The flags which describe this plane. TODO: Should be a TSet but that is misbehaving in the editor.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	TArray<EMagicLeapPlaneQueryFlags> PlaneFlags;

	/** ID of the plane result. This ID is persistent across queries */
	UPROPERTY(BlueprintReadOnly, Category = "Planes|MagicLeap")
	FGuid ID;

	/** ID of the inner plane. This ID is persistent across queries */
	UPROPERTY(BlueprintReadOnly, Category = "Planes|MagicLeap")
	FGuid InnerID;
};

/** Type used to represent a plane query. */
USTRUCT(BlueprintType)
struct MAGICLEAPPLANES_API FMagicLeapPlanesQuery
{
	GENERATED_BODY()

	FMagicLeapPlanesQuery() :
		  SearchVolume(nullptr)
		, MaxResults(10)
		, MinHoleLength(5.0f)
		, MinPlaneArea(100.0f)
		, SearchVolumePosition(0.0f, 0.0f, 0.0f)
		, SearchVolumeOrientation(0.0f, 0.0f, 0.0f, 1.0f)
		, SearchVolumeExtents(10.0f, 10.0f, 10.0f)
		, SimilarityThreshold(1.0f)
		, bSearchVolumeTrackingSpace(false)
		, bResultTrackingSpace(false)
	{}

	/** The flags to apply to this query. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	TArray<EMagicLeapPlaneQueryFlags> Flags;

	/** DEPRECATED. Use individual fields for setting search volume position, orientation and extents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap", meta = (DeprecatedProperty, DisplayName = "SearchVolume_DEPRECATED"))
	class UBoxComponent* SearchVolume;

	/**
		The maximum number of results that should be returned.  This is also the minimum expected size of the array of results
		passed to the MLPlanesGetResult function.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	int32 MaxResults;

	/**
		If #MLPlanesQueryFlag_IgnoreHoles is set to false, holes with a perimeter (in meters) smaller than this value will be
		ignored, and can be part of the plane.  This value cannot be lower than 0 (lower values will be capped to this minimum).
		A good default value is 50cm.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	float MinHoleLength;

	/**
		The minimum area (in squared meters) of planes to be returned.  This value cannot be lower than 400 (lower values
		will be capped to this minimum).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	float MinPlaneArea;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FVector SearchVolumePosition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FQuat SearchVolumeOrientation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FVector SearchVolumeExtents;

	/**
		The threshold used to compare incoming planes with any cached planes.
		Larger values reduce the amount of NewPlanes returned by a persistent query.
		Larger values increase the amount of error in the current set of planes.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	float SimilarityThreshold;

	/**
		A flag representing what coordinate space the search volume is in. 
		If set, the search volume is in HMD tracking space.
		If unset, the search volume is in world space.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	bool bSearchVolumeTrackingSpace;

	/**
		A flag representing what coordinate space the results are in.
		If set, the results are in HMD tracking space.
		If unset, the results are is in world space.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	bool bResultTrackingSpace;
};

USTRUCT(BlueprintType)
struct MAGICLEAPPLANES_API FMagicLeapPolygon
{
	GENERATED_BODY()

	/** The polygon that defines the region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	TArray<FVector> Vertices;
};

/** Coplanar connected line segments representing the outer boundary of an N-sided polygon. */
USTRUCT(BlueprintType)
struct MAGICLEAPPLANES_API FMagicLeapPlaneBoundary
{
	GENERATED_BODY()

	/** The polygon that defines the region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FMagicLeapPolygon Polygon;

	/** A polygon may contains multiple holes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	TArray<FMagicLeapPolygon> Holes;
};

/** Represents multiple regions on a 2D plane. */
USTRUCT(BlueprintType)
struct MAGICLEAPPLANES_API FMagicLeapPlaneBoundaries
{
	GENERATED_BODY()

	/** Plane ID, the same value associating to the ID in FMagicLeapPlaneResult if they belong to the same plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FGuid ID;

	/** The polygon that defines the region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	TArray<FMagicLeapPlaneBoundary> Boundaries;
};

/** Delegate used to convey the result of a plane query. */
DECLARE_DELEGATE_ThreeParams(FMagicLeapPlanesResultStaticDelegate, const bool, const TArray<FMagicLeapPlaneResult>&,const TArray<FMagicLeapPlaneBoundaries>&);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMagicLeapPlanesResultDelegate, const bool, bSuccess, const TArray<FMagicLeapPlaneResult>&, Planes, const TArray<FMagicLeapPlaneBoundaries>&, Polygons);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMagicLeapPlanesResultDelegateMulti, const bool, bSuccess, const TArray<FMagicLeapPlaneResult>&, Planes, const TArray<FMagicLeapPlaneBoundaries>&, Polygons);

/** Delegate used to convey the result of a persistent plane query. */
DECLARE_DELEGATE_SevenParams(FMagicLeapPersistentPlanesResultStaticDelegate, const bool, const FGuid&, const EMagicLeapPlaneQueryType, const TArray<FMagicLeapPlaneResult>&, const TArray<FGuid>&, const TArray<FMagicLeapPlaneBoundaries>&, const TArray<FGuid>&);
DECLARE_DYNAMIC_DELEGATE_SevenParams(FMagicLeapPersistentPlanesResultDelegate, const bool, bSuccess, const FGuid&, QueryHandle, const EMagicLeapPlaneQueryType, QueryType,const TArray<FMagicLeapPlaneResult>&, NewPlanes, const TArray<FGuid>&, RemovedPlaneIDs, const TArray<FMagicLeapPlaneBoundaries>&, NewPolygons, const TArray<FGuid>&, RemovedPolygonIDs);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SevenParams(FMagicLeapPersistentPlanesResultDelegateMulti, const bool, bSuccess, const FGuid&, QueryHandle, const EMagicLeapPlaneQueryType, QueryType,const TArray<FMagicLeapPlaneResult>&, NewPlanes, const TArray<FGuid>&, RemovedPlaneIDs, const TArray<FMagicLeapPlaneBoundaries>&, NewPolygons, const TArray<FGuid>&, RemovedPolygonIDs);

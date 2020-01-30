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

/** Represents a plane returned from the ML-API. */
USTRUCT(BlueprintType)
struct FMagicLeapPlaneResult
{
	GENERATED_BODY()

public:
	/** Position of the center of the plane in world coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FVector PlanePosition;

	/** Orientation of the plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FRotator PlaneOrientation;

	/** Orientation of the content with its up-vector orthogonal to the plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FRotator ContentOrientation;

	/** Width and height of the plane (in Unreal units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	FVector2D PlaneDimensions;

	/** The flags which describe this plane. TODO: Should be a TSet but that is misbehaving in the editor.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	TArray<EMagicLeapPlaneQueryFlags> PlaneFlags;

	/** ID of the plane result. This ID is persistent across queries */
	UPROPERTY(BlueprintReadOnly, Category = "Planes|MagicLeap")
	FGuid ID;
};

/** Type used to represent a plane query. */
USTRUCT(BlueprintType)
struct FMagicLeapPlanesQuery
{
	GENERATED_BODY()

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
};

USTRUCT(BlueprintType)
struct FMagicLeapPolygon
{
	GENERATED_BODY()

	/** The polygon that defines the region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes|MagicLeap")
	TArray<FVector> Vertices;
};

/** Coplanar connected line segments representing the outer boundary of an N-sided polygon. */
USTRUCT(BlueprintType)
struct FMagicLeapPlaneBoundary
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
struct FMagicLeapPlaneBoundaries
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
DECLARE_DELEGATE_ThreeParams(FMagicLeapPlanesResultStaticDelegate, const bool, const TArray<FMagicLeapPlaneResult>&, const TArray<FMagicLeapPlaneBoundaries>&);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMagicLeapPlanesResultDelegate, const bool, bSuccess, const TArray<FMagicLeapPlaneResult>&, Planes, const TArray<FMagicLeapPlaneBoundaries>&, Polygons);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMagicLeapPlanesResultDelegateMulti, const bool, bSuccess, const TArray<FMagicLeapPlaneResult>&, Planes, const TArray<FMagicLeapPlaneBoundaries>&, Polygons);

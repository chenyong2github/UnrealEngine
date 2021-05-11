// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Templates/PimplPtr.h"

// Double precision structures
#include "GeographicCoordinates.h"
#include "CartesianCoordinates.h"
#include "Ellipsoid.h"

// Using double precision maths from the Geometry plugin. 
#include "VectorTypes.h"
#include "MatrixTypes.h"
#include "Matrix4d.h"

#include "GeoReferencingSystem.generated.h"

UENUM(BlueprintType)
enum class EPlanetShape : uint8 {
	/**
	 * The world geometry coordinates are expressed in a projected space such as a Mercator projection.
	 * In this mode, Planet curvature is not considered and you might face errors related to projection on large environments
	 */
	FlatPlanet UMETA(DisplayName = "Flat Planet"),

	/**
	 * The world geometry coordinates are expressed in a planet wide Cartesian frame,
	 * placed on a specific location or at earth, or at the planet center.
	 * You might need dynamic rebasing to avoid precision issues at large scales.
	 */
	 RoundPlanet UMETA(DisplayName = "Round Planet"),
};

/**
 *
 */
UCLASS(hidecategories=(Transform, Replication, Actor, LOD, Cooking))
class GEOREFERENCING_API AGeoReferencingSystem : public AInfo
{
	GENERATED_BODY()

public:

	virtual void PostLoad() override;
	virtual void PostActorCreated() override;
	virtual void BeginDestroy() override;

	UFUNCTION(BlueprintPure, Category = "GeoReferencing", meta = (WorldContext = "WorldContextObject"))
	static AGeoReferencingSystem* GetGeoReferencingSystem(UObject* WorldContextObject);

	/**
	  * Convert a Vector expressed in ENGINE space to the PROJECTED CRS
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Transformations")
	void EngineToProjected(const FVector& EngineCoordinates, FCartesianCoordinates& ProjectedCoordinates);

	/**
	 * Convert a Vector expressed in PROJECTED CRS to ENGINE space
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Transformations")
	void ProjectedToEngine(const FCartesianCoordinates& ProjectedCoordinates, FVector& EngineCoordinates);

	// Engine <--> ECEF 

	/**
	 * Convert a Vector expressed in ENGINE space to the ECEF CRS
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Transformations")
	void EngineToECEF(const FVector& EngineCoordinates, FCartesianCoordinates& ECEFCoordinates);

	/**
	 * Convert a Vector expressed in ECEF CRS to ENGINE space
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Transformations")
	void ECEFToEngine(const FCartesianCoordinates& ECEFCoordinates, FVector& EngineCoordinates);

	// Projected <--> Geographic

	/**
	 * Convert a Coordinate expressed in PROJECTED CRS to GEOGRAPHIC CRS
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Transformations")
	void ProjectedToGeographic(const FCartesianCoordinates& ProjectedCoordinates, FGeographicCoordinates& GeographicCoordinates);

	/**
	 * Convert a Coordinate expressed in GEOGRAPHIC CRS to PROJECTED CRS
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Transformations")
	void GeographicToProjected(const FGeographicCoordinates& GeographicCoordinates, FCartesianCoordinates& ProjectedCoordinates);


	// Projected <--> ECEF
	/**
	 * Convert a Coordinate expressed in PROJECTED CRS to ECEF CRS
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Transformations")
	void ProjectedToECEF(const FCartesianCoordinates& ProjectedCoordinates, FCartesianCoordinates& ECEFCoordinates);

	/**
	 * Convert a Coordinate expressed in ECEF CRS to PROJECTED CRS
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Transformations")
	void ECEFToProjected(const FCartesianCoordinates& ECEFCoordinates, FCartesianCoordinates& ProjectedCoordinates);

	// Geographic <--> ECEF

	/**
	 * Convert a Coordinate expressed in GEOGRAPHIC CRS to ECEF CRS
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Transformations")
	void GeographicToECEF(const FGeographicCoordinates& GeographicCoordinates, FCartesianCoordinates& ECEFCoordinates);

	/**
	 * Convert a Coordinate expressed in ECEF CRS to GEOGRAPHIC CRS
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Transformations")
	void ECEFToGeographic(const FCartesianCoordinates& ECEFCoordinates, FGeographicCoordinates& GeographicCoordinates);


	// ENU & Transforms

	/**
	 * Get the East North Up vectors at a specific location
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|ENU")
	void GetENUVectorsAtEngineLocation(const FVector& EngineCoordinates, FVector& East, FVector& North, FVector& Up);

	/**
	 * Get the East North Up vectors at a specific location
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|ENU")
	void GetENUVectorsAtProjectedLocation(const FCartesianCoordinates& ProjectedCoordinates, FVector& East, FVector& North, FVector& Up);

	/**
	 * Get the East North Up vectors at a specific location
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|ENU")
	void GetENUVectorsAtGeographicLocation(const FGeographicCoordinates& GeographicCoordinates, FVector& East, FVector& North, FVector& Up);

	/**
	 * Get the East North Up vectors at a specific location
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|ENU")
	void GetENUVectorsAtECEFLocation(const FCartesianCoordinates& ECEFCoordinates, FVector& East, FVector& North, FVector& Up);

	/**
	 * Get the East North Up vectors at a specific location - Not in engine frame, but in pure ECEF Frame !
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|ENU")
	void GetECEFENUVectorsAtECEFLocation(const FCartesianCoordinates& ECEFCoordinates, FVector& ECEFEast, FVector& ECEFNorth, FVector& ECEFUp);

	/**
	 * Get the the transform to locate an object tangent to Ellipsoid at a specific location
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|TangentTransforms")
	FTransform GetTangentTransformAtEngineLocation(const FVector& EngineCoordinates);

	/**
	 * Get the the transform to locate an object tangent to Ellipsoid at a specific location
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|TangentTransforms")
	FTransform GetTangentTransformAtProjectedLocation(const FCartesianCoordinates& ProjectedCoordinates);

	/**
	 * Get the the transform to locate an object tangent to Ellipsoid at a specific location
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|TangentTransforms")
	FTransform GetTangentTransformAtGeographicLocation(const FGeographicCoordinates& GeographicCoordinates);

	/**
	 * Get the the transform to locate an object tangent to Ellipsoid at a specific location
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|TangentTransforms")
	FTransform GetTangentTransformAtECEFLocation(const FCartesianCoordinates& ECEFCoordinates);

	/**
	 * Set this transform to an Ellipsoid to have it positioned tangent to the origin.
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Misc")
	FTransform GetPlanetCenterTransform();

	// Public PROJ Utilities 

	/**
	 * Check if the string corresponds to a valid CRS descriptor
	 */
	UFUNCTION(BlueprintCallable, Category = "GeoReferencing|Misc")
	bool IsCRSStringValid(FString CRSString, FString& Error);


	//////////////////////////////////////////////////////////////////////////
	// General

	/**
	 * This mode has to be set consistently with the way you authored your ground geometry.
	 *  - For small environments, a projection is often applied and the world is considered as Flat
	 *  - For planet scale environments, projections is not suitable and the geometry is Rounded.
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeoReferencing")
	EPlanetShape PlanetShape = EPlanetShape::FlatPlanet;

	/**
	 * String that describes the PROJECTED CRS of choice. 
	 *    CRS can be identified by their code (EPSG:4326), a well-known text(WKT) string, or PROJ strings...
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "GeoReferencing")
	FString ProjectedCRS = FString(TEXT("EPSG:32617")); // UTM Zone 17 North - (Cary) https://epsg.io/32617

	/**
	 * String that describes the GEOGRAPHIC CRS of choice.
	 *    CRS can be identified by their code (EPSG:4326), a well-known text(WKT) string, or PROJ strings...
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "GeoReferencing")
	FString GeographicCRS = FString(TEXT("EPSG:4326")); // WGS84 https://epsg.io/4326

	//////////////////////////////////////////////////////////////////////////
	// Origin Location

	/**
	 * if true, the UE origin is located at the Planet Center, otherwise, 
	 * the UE origin is assuming to be defined at one specific point of 
	 * the planet surface, defined by the properties below. 
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeoReferencing|Origin Location", meta = (EditConditionHides, EditCondition = "PlanetShape==EPlanetShape::RoundPlanet"))
	bool bOriginAtPlanetCenter = false;

	/**
	 * if true, the UE origin georeference is expressed in the PROJECTED CRS. 
	 *     (NOT in ECEF ! - Projected worlds are the most frequent use case...)
	 * if false, the origin is located using geographic coordinates. 
	 * 
	 * WARNING : the location has to be expressed as Integer values because of accuracy. 
	 * Be very careful about that when authoring your data in external tools ! 
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeoReferencing|Origin Location", meta = (EditConditionHides, EditCondition = "bOriginAtPlanetCenter==false"))
	bool bOriginLocationInProjectedCRS = true;

	/**
	 * Latitude of UE Origin on planet
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeoReferencing|Origin Location", meta = (EditConditionHides, EditCondition = "!bOriginLocationInProjectedCRS && !bOriginAtPlanetCenter", ClampMin = "-90", ClampMax = "90"))
	int32 OriginLatitude = 35; // Around Epic Games HQ

	/**
	 * Longitude of UE Origin on planet
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeoReferencing|Origin Location", meta = (EditConditionHides, EditCondition = "!bOriginLocationInProjectedCRS && !bOriginAtPlanetCenter", ClampMin = "-180", ClampMax = "180"))
	int32 OriginLongitude = -78; // Around Epic Games HQ

	/**
	 * Altitude of UE Origin on planet
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeoReferencing|Origin Location", meta = (EditConditionHides, EditCondition = "!bOriginLocationInProjectedCRS && !bOriginAtPlanetCenter"))
	int32 OriginAltitude = 0; // For constituency, but should be 0

	/**
	 * Easting position of UE Origin on planet, express in the Projected CRS Frame
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeoReferencing|Origin Location", meta = (EditConditionHides, EditCondition = "bOriginLocationInProjectedCRS && !bOriginAtPlanetCenter"))
	int32 OriginProjectedCoordinatesEasting = 705000; // Around Epic Games HQ

	/**
	 * Northing position of UE Origin on planet, express in the Projected CRS Frame
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeoReferencing|Origin Location", meta = (EditConditionHides, EditCondition = "bOriginLocationInProjectedCRS && !bOriginAtPlanetCenter"))
	int32 OriginProjectedCoordinatesNorthing = 3960000; // Around Epic Games HQ

	/**
	 * Up position of UE Origin on planet, express in the Projected CRS Frame
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeoReferencing|Origin Location", meta = (EditConditionHides, EditCondition = "bOriginLocationInProjectedCRS && !bOriginAtPlanetCenter"))
	int32 OriginProjectedCoordinatesUp = 0;

	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void Initialize();

public:
	void ApplySettings();

private:
	class FGeoReferencingSystemInternals;
	TPimplPtr<FGeoReferencingSystemInternals> Impl;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/Box.h"
#include "ShapeFunctions.generated.h"


UCLASS(meta = (ScriptName = "GeometryScript_Ray"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_RayFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	/**
	 * Create a Ray from two points, placing the Origin at A and the Direction as Normalize(B-A)
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Ray")
	static UPARAM(DisplayName="Ray") FRay
	MakeRayFromPoints(const FVector& A, const FVector& B);

	/**
	 * Create a Ray from an Origin and Direction, with optionally non-normalized Direction
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Ray")
	static UPARAM(DisplayName="Ray") FRay 
	MakeRayFromPointDirection(const FVector& Origin, const FVector& Direction, bool bDirectionIsNormalized = true);

	/**
	 * Apply the given Transform to the given Ray, or optionally the Transform Inverse, and return the new transformed Ray
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Ray", meta=(ScriptMethod))
	static UPARAM(DisplayName="Transformed Ray") FRay 
	GetTransformedRay(const FRay& Ray, const FTransform& Transform, bool bInvert = false);

	/**
	 * Get a Point at the given Distance along the Ray (Origin + Distance*Direction)
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Ray", meta=(ScriptMethod))
	static UPARAM(DisplayName="Point") FVector 
	GetRayPoint(const FRay& Ray, double Distance);

	/**
	 * Project the given Point onto the closest point along the Ray, and return the Ray Parameter/Distance at that Point
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Ray", meta=(ScriptMethod))
	static UPARAM(DisplayName="Ray Paramater") double 
	GetRayParameter(const FRay& Ray, const FVector& Point);

	/**
	 * Get the distance from Point to the closest point on the Ray
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Ray", meta=(ScriptMethod))
	static UPARAM(DisplayName="Distance") double 
	GetRayPointDistance(const FRay& Ray, const FVector& Point);

	/**
	 * Get the closest point on the Ray to the given Point
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Ray", meta=(ScriptMethod))
	static UPARAM(DisplayName="Closest Point") FVector 
	GetRayClosestPoint(const FRay& Ray, const FVector& Point);

	/**
	 * Check if the Ray intersects a Sphere defined by the SphereCenter and SphereRadius.
	 * This function returns two intersection distances (ray parameters). If the ray grazes the sphere, both
	 * distances will be the same, and if it misses, they will be MAX_FLOAT. 
	 * Use the function GetRayPoint to convert the distances to points on the ray/sphere.
	 * 
	 * @param Distance1 Distance along ray (Ray Parameter) to first/closer intersection point with sphere
	 * @param Distance2 Distance along ray (Ray Parameter) to second/further intersection point with sphere
	 * @return true if ray intersects sphere
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Ray", meta=(ScriptMethod))
	static UPARAM(DisplayName="Intersects") bool 
	GetRaySphereIntersection(const FRay& Ray, const FVector& SphereCenter, double SphereRadius, double& Distance1, double& Distance2);

	/**
	 * Check if the Ray intersects a Sphere defined by the SphereCenter and SphereRadius.
	 * @param HitDistance Distance along the ray (Ray Parameter) to first intersection point with the Box
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Ray", meta=(ScriptMethod))
	static UPARAM(DisplayName="Intersects") bool 
	GetRayBoxIntersection(const FRay& Ray, const FBox& Box, double& HitDistance);

};


UCLASS(meta = (ScriptName = "GeometryScript_Box"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_BoxFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	/**
	 * Create a Box from a Center point and X/Y/Z Dimensions (*not* Extents, which are half-dimensions)
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box")
	static UPARAM(DisplayName="Box") FBox 
	MakeBoxFromCenterSize(const FVector& Center, const FVector& Dimensions);

	/**
	 * Get the Center point and X/Y/Z Dimensions of a Box (full dimensions, not Extents)
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static void GetBoxCenterSize(const FBox& Box, FVector& Center, FVector& Dimensions);


	/**
	 * Get the position of a corner of the Box. Corners are indexed from 0 to 7, using
	 * an ordering where 0 is the Min corner, 1/2/3 are +Z/+Y/+X from the Min corner, 
	 * 7 is the Max corner, and 4/5/6 are -Z/-Y/-X from the Max corner.
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static FVector GetBoxCorner(const FBox& Box, int CornerIndex);

	/**
	 * Get the position of the center of a face of the Box. Faces are indexed from 0 to 5,
	 * using an ordering where 0/1 are the MinZ/MaxZ faces, 2/3 are MinY/MaxY, and 4/5 are MinX/MaxX
	 * @param FaceNormal returned Normal vector of the identified face
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static FVector GetBoxFaceCenter(const FBox& Box, int FaceIndex, FVector& FaceNormal);

	/**
	 * Get the Volume and Surface Area of a Box
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static void GetBoxVolumeArea(const FBox& Box, double& Volume, double& SurfaceArea);

	/**
	 * Get the input Box expanded by adding the ExpandBy parameter to both the Min and Max.
	 * Dimensions will be clamped to the center point if any of ExpandBy are larger than half the box size
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static FBox GetExpandedBox(const FBox& Box, const FVector& ExpandBy);

	/**
	 * Apply the input Transform to the corners of the input Box, and return the new Box containing those points
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static FBox GetTransformedBox(const FBox& Box, const FTransform& Transform);

	/**
	 * Test if Box1 and Box2 intersect
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static bool TestBoxBoxIntersection(const FBox& Box1, const FBox& Box2);

	/**
	 * Find the Box formed by the intersection of Box1 and Box2
	 * @param bIsIntersecting if the boxes do not intersect, this will be returned as false, otherwise true
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static FBox FindBoxBoxIntersection(const FBox& Box1, const FBox& Box2, bool& bIsIntersecting);

	/**
	 * Calculate the minimum distance between Box1 and Box2
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static double GetBoxBoxDistance(const FBox& Box1, const FBox& Box2);

	/**
	 * Test if a Point is inside the Box, returning true if so, otherwise false
	 * @param bConsiderOnBoxAsInside if true, a point lying on the box face is considered "inside", otherwise it is considered "outside"
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static bool TestPointInsideBox(const FBox& Box, const FVector& Point, bool bConsiderOnBoxAsInside = true);

	/**
	 * Find the point on the faces of the Box that is closest to the input Point.
	 * If the Point is inside the Box, it is returned, ie points Inside do not project to the Box Faces
	 * @param bIsInside if the Point is inside the Box, this will return as true, otherwise false
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static FVector FindClosestPointOnBox(const FBox& Box, const FVector& Point, bool& bIsInside);

	/**
	 * Calculate the minimum distance between the Box and the Point
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static double GetBoxPointDistance(const FBox& Box, const FVector& Point);

	/**
	 * Check if the Box intersects a Sphere defined by the SphereCenter and SphereRadius
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|Shapes|Box", meta=(ScriptMethod))
	static bool TestBoxSphereIntersection(const FBox& Box, const FVector& SphereCenter, double SphereRadius);

};


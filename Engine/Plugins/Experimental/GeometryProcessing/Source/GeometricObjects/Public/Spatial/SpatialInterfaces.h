// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp SpatialInterfaces

#pragma once

#include "VectorTypes.h"
#include "RayTypes.h"


/**
 * ISpatial is a base interface for spatial queries
 */
class ISpatial
{
public:
	virtual ~ISpatial() {}

	/**
	 * @return true if this object supports point-containment inside/outside queries
	 */
	virtual bool SupportsPointContainment() = 0;

	/**
	 * @return true if the query point is inside the object
	 */
	virtual bool IsInside(const FVector3d & Point) = 0;
};



/**
 * IMeshSpatial is an extension of ISpatial specifically for meshes
 */
class IMeshSpatial : public ISpatial
{
public:
	virtual ~IMeshSpatial() {}

	/**
	 * @return true if this object supports nearest-triangle queries
	 */
	virtual bool SupportsNearestTriangle() = 0;

	/**
	 * @param Query point
	 * @param NearestDistSqrOut returned nearest squared distance, if triangle is found
	 * @param MaxDistance maximum search distance
	 * @return ID of triangle nearest to Point within MaxDistance, or InvalidID if not found
	 */
	virtual int FindNearestTriangle(const FVector3d& Point, double& NearestDistSqrOut, double MaxDistance = TNumericLimits<double>::Max()) = 0;


	/**
	 * @return true if this object supports ray-triangle intersection queries
	 */
	virtual bool SupportsTriangleRayIntersection() = 0;

	/**
	 * @param Ray query ray
	 * @param MaxDistance maximum hit distance
	 * @return ID of triangle intersected by ray within MaxDistance, or InvalidID if not found
	 */
	inline virtual int FindNearestHitTriangle(const FRay3d& Ray, double MaxDistance = TNumericLimits<double>::Max())
	{
		double NearestT;
		int TID;
		FindNearestHitTriangle(Ray, NearestT, TID, MaxDistance);
		return TID;
	}

	/**
	 * Find nearest triangle from the given ray
	 * @param Ray query ray
	 * @param NearestT returned-by-reference parameter of the nearest hit
	 * @param TID returned-by-reference ID of triangle intersected by ray within MaxDistance, or InvalidID if not found
	 * @param MaxDistance maximum hit distance
	 * @return true if hit, false if no hit found
	 */
	virtual bool FindNearestHitTriangle(const FRay3d& Ray, double& NearestT, int& TID, double MaxDistance = TNumericLimits<double>::Max()) = 0;


};



/**
 * IProjectionTarget is an object that supports projecting a 3D point onto it
 */
class IProjectionTarget
{
public:
	virtual ~IProjectionTarget() {}

	/**
	 * @param Point the point to project onto the target
	 * @param Identifier client-defined integer identifier of the point (may not be used)
	 * @return position of Point projected onto the target
	 */
	virtual FVector3d Project(const FVector3d& Point, int Identifier = -1) = 0;
};



/**
 * IOrientedProjectionTarget is a projection target that can return a normal in addition to the projected point
 */
class IOrientedProjectionTarget : public IProjectionTarget
{
public:
	virtual ~IOrientedProjectionTarget() {}

	/**
	 * @param Point the point to project onto the target
	 * @param Identifier client-defined integer identifier of the point (may not be used)
	 * @return position of Point projected onto the target
	 */
	virtual FVector3d Project(const FVector3d& Point, int Identifier = -1) override = 0;

	/**
	 * @param Point the point to project onto the target
	 * @param ProjectNormalOut the normal at the projection point 
	 * @param Identifier client-defined integer identifier of the point (may not be used)
	 * @return position of Point projected onto the target
	 */
	virtual FVector3d Project(const FVector3d& Point, FVector3d& ProjectNormalOut, int Identifier = -1) = 0;
};



/**
 * IIntersectionTarget is an object that can be intersected with a ray
 */
class IIntersectionTarget
{
public:
	virtual ~IIntersectionTarget() {}

	/**
	 * @return true if RayIntersect will return a normal
	 */
	virtual bool HasNormal() = 0;

	/**
	 * @param Ray query ray
	 * @param HitOut returned hit point
	 * @param HitNormalOut returned hit point normal
	 * @return true if ray hit the object
	 */
	virtual bool RayIntersect(const FRay3d& Ray, FVector3d& HitOut, FVector3d& HitNormalOut) = 0;
};

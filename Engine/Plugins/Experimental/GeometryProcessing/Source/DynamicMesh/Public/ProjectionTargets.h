// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// Port of geometry3cpp ProjectionTargets

#include "DynamicMeshAABBTree3.h"
#include "Distance/DistPoint3Triangle3.h"


/**
 * MeshProjectionTarget provides an IProjectionTarget interface to a FDynamicMesh + FDynamicMeshAABBTree3
 * Use to project points to mesh surface.
 */
class MeshProjectionTarget : public IOrientedProjectionTarget
{
public:
	/** The mesh to project onto */
	FDynamicMesh3* Mesh;
	/** An AABBTree for Mesh */
	FDynamicMeshAABBTree3* Spatial;

	~MeshProjectionTarget() {}

	MeshProjectionTarget(FDynamicMesh3* MeshIn, FDynamicMeshAABBTree3* SpatialIn)
	{
		Mesh = MeshIn;
		Spatial = SpatialIn;
	}


	/**
	 * @return Projection of Point onto this target
	 */
	virtual FVector3d Project(const FVector3d& Point, int Identifier = -1) override
	{
		double fDistSqr;
		int tNearestID = Spatial->FindNearestTriangle(Point, fDistSqr);
		FTriangle3d Triangle;
		Mesh->GetTriVertices(tNearestID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);

		FDistPoint3Triangle3d DistanceQuery(Point, Triangle);
		DistanceQuery.GetSquared();
		return DistanceQuery.ClosestTrianglePoint;
	}

	/**
	 * @return Projection of Point onto this target, and set ProjectNormalOut to the triangle normal at the returned point (*not* interpolated vertex normal)
	 */
	virtual FVector3d Project(const FVector3d& Point, FVector3d& ProjectNormalOut, int Identifier = -1) override
	{
		double fDistSqr;
		int tNearestID = Spatial->FindNearestTriangle(Point, fDistSqr);
		FTriangle3d Triangle;
		Mesh->GetTriVertices(tNearestID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);

		ProjectNormalOut = Triangle.Normal();

		FDistPoint3Triangle3d DistanceQuery(Point, Triangle);
		DistanceQuery.GetSquared();
		return DistanceQuery.ClosestTrianglePoint;
	}


};
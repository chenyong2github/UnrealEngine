// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// Port of geometry3cpp ProjectionTargets

#include "DynamicMeshAABBTree3.h"


/**
 * FMeshProjectionTarget provides an IProjectionTarget interface to a FDynamicMesh + FDynamicMeshAABBTree3
 * Use to project points to mesh surface.
 */
class DYNAMICMESH_API FMeshProjectionTarget : public IOrientedProjectionTarget
{
public:
	/** The mesh to project onto */
	const FDynamicMesh3* Mesh = nullptr;
	/** An AABBTree for Mesh */
	FDynamicMeshAABBTree3* Spatial = nullptr;

	~FMeshProjectionTarget() {}

	FMeshProjectionTarget()
	{
	}

	FMeshProjectionTarget(const FDynamicMesh3* MeshIn, FDynamicMeshAABBTree3* SpatialIn)
	{
		Mesh = MeshIn;
		Spatial = SpatialIn;
	}


	/**
	 * @return Projection of Point onto this target
	 */
	virtual FVector3d Project(const FVector3d& Point, int Identifier = -1) override;

	/**
	 * @return Projection of Point onto this target, and set ProjectNormalOut to the triangle normal at the returned point (*not* interpolated vertex normal)
	 */
	virtual FVector3d Project(const FVector3d& Point, FVector3d& ProjectNormalOut, int Identifier = -1) override;


};
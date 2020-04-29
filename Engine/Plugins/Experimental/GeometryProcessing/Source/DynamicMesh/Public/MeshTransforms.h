// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp MeshWeights

#pragma once

#include "DynamicMesh3.h"


/**
 * Utility functions for applying transformations to meshes
 */
namespace MeshTransforms
{
	
	/**
	 * Apply Translation to vertex positions of Mesh. Does not modify any other attributes.
	 */
	DYNAMICMESH_API void Translate(FDynamicMesh3& Mesh, const FVector3d& Translation);

	/**
	 * Apply Scale to vertex positions of Mesh, relative to given Origin. Does not modify any other attributes (normals/etc)
	 */
	DYNAMICMESH_API void Scale(FDynamicMesh3& Mesh, const FVector3d& Scale, const FVector3d& Origin);

	/**
	 * Transform Mesh into local coordinates of Frame
	 */
	DYNAMICMESH_API void WorldToFrameCoords(FDynamicMesh3& Mesh, const FFrame3d& Frame);

	/**
	 * Transform Mesh out of local coordinates of Frame
	 */
	DYNAMICMESH_API void FrameCoordsToWorld(FDynamicMesh3& Mesh, const FFrame3d& Frame);


	/**
	 * Apply given Transform to a Mesh.
	 * Modifies Vertex Positions and Normals, and any Per-Triangle Normal Overlays
	 */
	DYNAMICMESH_API void ApplyTransform(FDynamicMesh3& Mesh, const FTransform3d& Transform);


	/**
	 * Apply inverse of given Transform to a Mesh.
	 * Modifies Vertex Positions and Normals, and any Per-Triangle Normal Overlays
	 */
	DYNAMICMESH_API void ApplyTransformInverse(FDynamicMesh3& Mesh, const FTransform3d& Transform);


	/**
	 * Apply given Transform to a Mesh.
	 * Modifies Vertex Positions and Normals, and any Per-Triangle Normal Overlays
	 */
	DYNAMICMESH_API void ApplyTransform(FDynamicMesh3& Mesh, 
		TFunctionRef<FVector3d(const FVector3d&)> PositionTransform,
		TFunctionRef<FVector3f(const FVector3f&)> NormalTransform);



};
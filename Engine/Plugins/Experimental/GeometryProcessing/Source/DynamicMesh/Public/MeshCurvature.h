// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"

namespace UE
{
	/**
	 * Implementations of various mesh curvatures and curvature-related metrics
	 */
	namespace MeshCurvature
	{
		/**
		 * Calculate the Discrete Mean-Curvature Normal at a vertex as defined by discrete differential geometry.
		 * Based on Eq 8 from "Discrete Differential-Geometry Operators for Triangulated 2-Manifolds", Meyer et al 2002
		 * The Discrete Mean-Curvature Normal is (2.0 * MeanCurvature * SurfaceNormal)    ((unclear why it has the 2.0))
		 */
		DYNAMICMESH_API FVector3d MeanCurvatureNormal(const FDynamicMesh3& Mesh, int32 VertexIndex);


		/**
		 * Calculate the Discrete Mean-Curvature Normal at a vertex as defined by discrete differential geometry.
		 * Based on Eq 8 from "Discrete Differential-Geometry Operators for Triangulated 2-Manifolds", Meyer et al 2002
		 * The Discrete Mean-Curvature Normal is (2.0 * MeanCurvature * SurfaceNormal)    ((unclear why it has the 2.0))
		 * @param VertexPositionFunc use positions returned by this function instead of mesh positions
		 */
		DYNAMICMESH_API FVector3d MeanCurvatureNormal(const FDynamicMesh3& Mesh, int32 VertexIndex, TFunctionRef<FVector3d(int32)> VertexPositionFunc);



		/**
		 * Calculate the Discrete Gaussian Curvature at a vertex as defined by discrete differential geometry
		 * Based on Eq 9 from "Discrete Differential-Geometry Operators for Triangulated 2-Manifolds", Meyer et al 2002
		 */
		DYNAMICMESH_API double GaussianCurvature(const FDynamicMesh3& Mesh, int32 VertexIndex);

		/**
		 * Calculate the Discrete Gaussian Curvature at a vertex as defined by discrete differential geometry
		 * Based on Eq 9 from "Discrete Differential-Geometry Operators for Triangulated 2-Manifolds", Meyer et al 2002
		 * @param VertexPositionFunc use positions returned by this function instead of mesh positions
		 */
		DYNAMICMESH_API double GaussianCurvature(const FDynamicMesh3& Mesh, int32 VertexIndex, TFunctionRef<FVector3d(int32)> VertexPositionFunc);

	}
}
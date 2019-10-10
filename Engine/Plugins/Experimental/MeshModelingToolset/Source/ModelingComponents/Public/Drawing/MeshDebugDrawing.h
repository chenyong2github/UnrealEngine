// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshAttributeSet.h"
#include "FrameTypes.h"

class FPrimitiveDrawInterface;

/**
 * drawing utility functions useful for debugging. These are generally not performant.
 */
namespace MeshDebugDraw
{

	/**
	 * Draw normals of mesh overlay as lines
	 * @param Overlay The overlay that provides the normals (and mesh positions)
	 * @param Length length of the lines in world space
	 * @param Color color of the lines
	 * @param Thickness thickness of the lines
	 * @param bScreenSpace is the thickness in pixel or world space
	 * @param PDI drawing interface
	 * @param Transform transform applied to the line endpoints
	 */
	void MODELINGCOMPONENTS_API DrawNormals(
		const FDynamicMeshNormalOverlay* Overlay,
		float Length, FColor Color, float Thickness, bool bScreenSpace,
		FPrimitiveDrawInterface* PDI, const FTransform& Transform);


	/**
	 * Draw vertices of mesh as points
	 * @param Mesh The Mesh that provides the vertices
	 * @param Indices the list of indices
	 * @param PointSize the size of the points in screen space
	 * @param Color color of the lines
	 * @param PDI drawing interface
	 * @param Transform transform applied to the vertex positions
	 */
	void MODELINGCOMPONENTS_API DrawVertices(
		const FDynamicMesh3* Mesh, const TArray<int>& Indices,
		float PointSize, FColor Color,
		FPrimitiveDrawInterface* PDI, const FTransform& Transform);
	void MODELINGCOMPONENTS_API DrawVertices(
		const FDynamicMesh3* Mesh, const TSet<int>& Indices,
		float PointSize, FColor Color,
		FPrimitiveDrawInterface* PDI, const FTransform& Transform);


	/**
	 * Draw mesh triangle centroids as points
	 * @param Mesh The Mesh that provides the vertices
	 * @param Indices the list of triangle indices
	 * @param PointSize the size of the points in screen space
	 * @param Color color of the lines
	 * @param PDI drawing interface
	 * @param Transform transform applied to the centroid positions
	 */
	void MODELINGCOMPONENTS_API DrawTriCentroids(
		const FDynamicMesh3* Mesh, const TArray<int>& Indices,
		float PointSize, FColor Color,
		FPrimitiveDrawInterface* PDI, const FTransform& Transform);


	/**
	 * Draw a basic 2D grid with a number of lines at given spacing
	 * @param LocalFrame Pre-transform frame of grid (grid lies in XY plane). 
	 * @param GridLines number of grid lines. If odd, there is a center-line, if even then frame center is at center of a grid square
	 * @param GridLineSpacing spacing size between grid lines
	 * @param LineWidth thickness of the lines in screen space
	 * @param Color color of the lines
	 * @param DepthPriority drawing depth priority
	 * @param PDI drawing interface
	 * @param Transform transform applied to LocalFrame. Pass as Identity() if you have world frame.
	 * @return 
	 */
	void MODELINGCOMPONENTS_API DrawSimpleGrid(
		const FFrame3f& LocalFrame, int GridLines, float GridLineSpacing,
		float LineWidth, FColor Color, bool bDepthTested,
		FPrimitiveDrawInterface* PDI, const FTransform& Transform);

}
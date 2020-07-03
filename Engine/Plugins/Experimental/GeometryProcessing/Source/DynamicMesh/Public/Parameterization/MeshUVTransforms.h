// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"



/**
 * Utility functions for applying transformations to meshes
 */
namespace UE
{
	namespace MeshUVTransforms
	{
		enum class EIslandPositionType
		{
			CurrentPosition,
			MinBoxCornerToOrigin,
			CenterToOrigin
		};

		/**
		 * Recenter given UV element IDs using given IslandPositionType strategy (based on centroid, bounding-box, etc)
		 * and then apply uniform scaling 
		 */
		DYNAMICMESH_API void RecenterScale(FDynamicMeshUVOverlay* UVOverlay, const TArray<int32>& UVElementIDs,
			EIslandPositionType NewPosition, double UVScale);
		
		/**
		 * Fit given UV element IDs inside the specified Box. 
		 * @param bUniformScale if false, UVs will be non-uniformly scaled to maximally fill the box
		 */
		DYNAMICMESH_API void FitToBox(FDynamicMeshUVOverlay* UVOverlay, const TArray<int32>& UVElementIDs, const FAxisAlignedBox2d& Box, bool bUniformScale = true);

		/**
		 * Fit all UV element IDs of the UV Overlay inside the specified Box.
		 * @param bUniformScale if false, UVs will be non-uniformly scaled to maximally fill the box
		 */
		DYNAMICMESH_API void FitToBox(FDynamicMeshUVOverlay* UVOverlay, const FAxisAlignedBox2d& Box, bool bUniformScale = true);
	}
}
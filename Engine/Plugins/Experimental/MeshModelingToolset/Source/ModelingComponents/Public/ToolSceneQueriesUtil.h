// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveTool.h"
#include "InteractiveToolManager.h"
#include "VectorTypes.h"

/**
 * Utility functions for Tool implementations to use to do scene queries, generally via IToolsContextQueriesAPI
 */
namespace ToolSceneQueriesUtil
{
	/**
	 * @return global visual angle snap threshold (default is 1 degree)
	 */
	MODELINGCOMPONENTS_API double GetDefaultVisualAngleSnapThreshD();

	/**
	 * Test if two points are close enough to snap together.
	 * This is done by computing visual angle between points for current camera position.
	 * @param VisualAngleThreshold visual angle threshold to use. If 0, GetDefaultVisualAngleSnapThresh() is used
	 */
	MODELINGCOMPONENTS_API bool PointSnapQuery(const UInteractiveTool* Tool, const FVector3d& Point1, const FVector3d& Point2, double VisualAngleThreshold = 0);

	/**
	 * Test if two points are close enough to snap together.
	 * This is done by computing visual angle between points for current camera position.
	 * @param VisualAngleThreshold visual angle threshold to use. If 0, GetDefaultVisualAngleSnapThresh() is used
	 */
	MODELINGCOMPONENTS_API bool PointSnapQuery(const FViewCameraState& CameraState, const FVector3d& Point1, const FVector3d& Point2, double VisualAngleThreshold = 0);


	/**
	 * @return visual angle between two 3D points, relative to the current camera position
	 */
	MODELINGCOMPONENTS_API double CalculateViewVisualAngleD(const UInteractiveTool* Tool, const FVector3d& Point1, const FVector3d& Point2);


	/**
	 * @return visual angle between two 3D points, relative to the current camera position
	 */
	MODELINGCOMPONENTS_API double CalculateViewVisualAngleD(const FViewCameraState& CameraState, const FVector3d& Point1, const FVector3d& Point2);


	/**
	 * @return (approximate) 3D dimension that corresponds to a radius of target visual angle around Point, for current camera position
	 */
	MODELINGCOMPONENTS_API double CalculateDimensionFromVisualAngleD(const UInteractiveTool* Tool, const FVector3d& Point, double TargetVisualAngleDeg);

	/**
	 * @return (approximate) 3D dimension that corresponds to a radius of target visual angle around Point
	 */
	MODELINGCOMPONENTS_API double CalculateDimensionFromVisualAngleD(const FViewCameraState& CameraState, const FVector3d& Point, double TargetVisualAngleDeg);



	/**
	 * @return false if point is not currently visible (approximately)
	 */
	MODELINGCOMPONENTS_API bool IsPointVisible(const FViewCameraState& CameraState, const FVector3d& Point);



	/**
	 * FSnapGeometry stores information about geometry data of a snap, which we might use for highlights/etc
	 */
	struct FSnapGeometry
	{
		/** Geometry that was snapped to. only PointCount elements are initialized */
		FVector3d Points[3];
		/** Number of initialized elements in Points */
		int PointCount = 0;
	};


	/**
	 * Run a query against the scene to find the best SnapPointOut for the given Point
	 * @param bVertices if true, try snapping to mesh vertices in the scene
	 * @param bEdges if true, try snapping to mesh triangle edges in the scene
	 * @param VisualAngleThreshold visual angle threshold to use. If 0, GetDefaultVisualAngleSnapThresh() is used
	 * @param DebugTriangleOut if non-null, triangle containing snap is returned if a snap is found
	 * @return true if a valid snap point was found
	 */
	MODELINGCOMPONENTS_API bool FindSceneSnapPoint(const UInteractiveTool* Tool, const FVector3d& Point, FVector3d& SnapPointOut,
		bool bVertices = true, bool bEdges = false, double VisualAngleThreshold = 0, 
		FSnapGeometry* SnapGeometry = nullptr, FVector* DebugTriangleOut = nullptr);
}
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"
#include "ShapeApproximation/SimpleShapeSet3.h"

class FProgressCancel;

/**
 * EDetectedSimpleShapeType is used to identify auto-detected simple shapes for a mesh/etc
 */
enum class EDetectedSimpleShapeType
{
	/** Object is not a simple shape */
	None = 0,
	/** Object has been identified as a sphere */
	Sphere = 2,
	/** Object has been identified as a box */
	Box = 4,
	/** Object has been identified as a capsule */
	Capsule = 8,
	/** Object has been identified as a Convex */
	Convex = 16
};



/**
 * FMeshSimpleShapeApproximation can calculate various "simple" shape approximations for a set of meshes,
 * by fitting various primitives/hulls/etc to each mesh. The assumption is that the input mesh(es) are
 * already partitioned into pieces.
 *
 * There are various Generate_X() functions which apply different strategies, generally to fit a containing
 * simple shape or hull to the mesh. However in addition to these explicit strategies, input meshes that 
 * are very close to approximations of spheres/boxes/capsules (ie basically meshed versions of these primitives)
 * can be identified and used directly, skipping the fitting process.
 *
 */
class DYNAMICMESH_API FMeshSimpleShapeApproximation
{
public:
	
	//
	// configuration parameters
	//

	/** Should spheres be auto-detected */
	bool bDetectSpheres = true;
	/** Should boxes be auto-detected */
	bool bDetectBoxes = true;
	/** Should capsules be auto-detected */
	bool bDetectCapsules = true;
	/** Should convex be auto-detected */
	bool bDetectConvexes = true;

	/** minimal dimension of fit shapes, eg thickness/radius/etc (currently only enforced in certain cases) */
	double MinDimension = 0.0;

	/** should hulls be simplified as a post-process */
	bool bSimplifyHulls = true;
	/** target number of triangles when simplifying 3D convex hulls */
	int32 HullTargetFaceCount = 50;
	/** simplification tolerance when simplifying 2D convex hulls, eg for swept/projected hulls */
	double HullSimplifyTolerance = 1.0;

	bool bUseExactComputationForBox = false;

	//
	// setup/initialization
	//


	/**
	 * Initialize internal mesh sets. This also detects/caches the precise simple shape fits controlled
	 * by bDetectSpheres/etc above, so those cannot be modified without calling InitializeSourceMeshes() again.
	 * The TSharedPtrs are stored, rather than making a copy of the input meshes
	 * @param bIsParallelSafe if true
	 */
	void InitializeSourceMeshes(const TArray<const FDynamicMesh3*>& InputMeshSet);


	//
	// approximation generators
	//

	/**
	 * Fit containing axis-aligned boxes to each input mesh and store in ShapeSetOut
	 */
	void Generate_AlignedBoxes(FSimpleShapeSet3d& ShapeSetOut);

	/**
	 * Fit containing minimal-volume oriented boxes to each input mesh and store in ShapeSetOut
	 */
	void Generate_OrientedBoxes(FSimpleShapeSet3d& ShapeSetOut, FProgressCancel* Progress = nullptr);

	/**
	 * Fit containing minimal-volume spheres to each input mesh and store in ShapeSetOut
	 */
	void Generate_MinimalSpheres(FSimpleShapeSet3d& ShapeSetOut);

	/**
	 * Fit containing approximate-minimum-volume capsules to each input mesh and store in ShapeSetOut
	 * @warning the capsule is fit by first fitting a line to the vertices, and then containing the points, so the fit can deviate quite a bit from a truly "minimal" capsule
	 */
	void Generate_Capsules(FSimpleShapeSet3d& ShapeSetOut);

	/**
	 * Calculate 3D Convex Hulls for each input mesh and store in ShapeSetOut.
	 * Each convex hull is stored as a triangle mesh, and optionally simplified if bSimplifyHulls=true
	 */
	void Generate_ConvexHulls(FSimpleShapeSet3d& ShapeSetOut);


	/** Type/Mode for deciding 3D axis to use in Generate_ProjectedHulls() */
	enum class EProjectedHullAxisMode
	{
		/** Use Unit X axis */
		X = 0,
		/** Use Unit Y axis */
		Y = 1,
		/** Use Unit Z axis */
		Z = 2,
		/** Use X/Y/Z axis with smallest axis-aligned-bounding-box dimension */
		SmallestBoxDimension = 3,
		/** Compute projected hull for each of X/Y/Z axes and use the one that has the smallest volume  */
		SmallestVolume = 4
	};

	/**
	 * Calculate Projected Convex Hulls for each input mesh and store in ShapeSetOut.
	 * A Projected Hull is computed by first projecting all the mesh vertices to a plane, computing a 2D convex hull polygon,
	 * and then sweeping the polygon in 3D to contain all the mesh vertices. The 2D convex hull polygons
	 * are optionally simplified if bSimplifyHulls=true.
	 * @param AxisMode which axis to use for the planar projection
	 */
	void Generate_ProjectedHulls(FSimpleShapeSet3d& ShapeSetOut, EProjectedHullAxisMode AxisMode);


	/**
	 * Fit containing axis-aligned box, oriented box, capsule, and sphere to each input mesh, and
	 * store the one with smallest volume in ShapeSetOut
	 */
	void Generate_MinVolume(FSimpleShapeSet3d& ShapeSetOut);



protected:
	TArray<const FDynamicMesh3*> SourceMeshes;

	struct FSourceMeshCache
	{
		EDetectedSimpleShapeType DetectedType = EDetectedSimpleShapeType::None;

		FSphere3d DetectedSphere;
		FOrientedBox3d DetectedBox;
		FCapsule3d DetectedCapsule;
	};
	TArray<FSourceMeshCache> SourceMeshCaches;


	void DetectAndCacheSimpleShapeType(const FDynamicMesh3* SourceMesh, FSourceMeshCache& CacheOut);
	bool GetDetectedSimpleShape(const FSourceMeshCache& Cache,
		FSimpleShapeSet3d& ShapeSetOut, FCriticalSection& ShapeSetLock);
};



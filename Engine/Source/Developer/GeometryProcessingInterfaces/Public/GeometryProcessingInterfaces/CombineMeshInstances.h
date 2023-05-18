// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PhysicsEngine/AggregateGeom.h"

class UPrimitiveComponent;
class UStaticMesh;
class UMaterialInterface;


/**
 * The CombineMeshInstances modular feature is used to provide a mechanism
 * for merging a set of instances of meshes (ie mesh + transform + materials + ...)
 * into a smaller set of meshes. Generally this involves creating simpler versions
 * of the instances and appending them into one or a small number of combined meshes.
 */
class IGeometryProcessing_CombineMeshInstances : public IModularFeature
{
public:
	virtual ~IGeometryProcessing_CombineMeshInstances() {}


	enum class EMeshDetailLevel
	{
		Base = 0,
		Standard = 1,
		Small = 2,
		Decorative = 3
	};


	struct FMeshInstanceGroupData
	{
		TArray<UMaterialInterface*> MaterialSet;
		
		bool bHasConstantOverrideVertexColor = false;
		FColor OverrideVertexColor;
	};


	struct FBaseMeshInstance
	{
		EMeshDetailLevel DetailLevel = EMeshDetailLevel::Standard;
		TArray<FTransform3d> TransformSequence;
		int32 GroupDataIndex = -1;		// index into FInstanceSet::InstanceGroupDatas
	};


	struct FStaticMeshInstance : FBaseMeshInstance
	{
		UStaticMesh* SourceMesh = nullptr;
		UPrimitiveComponent* SourceComponent = nullptr;
		int32 SourceInstanceIndex = 0;
	};


	struct FInstanceSet
	{
		TArray<FStaticMeshInstance> StaticMeshInstances;

		// sets of data shared across multiple instances
		TArray<FMeshInstanceGroupData> InstanceGroupDatas;
	};


	enum class ERemoveHiddenFacesMode
	{
		None = 0,
		Fastest = 1,

		ExteriorVisibility = 5,
		OcclusionBased = 6
	};


	enum class EVertexColorMappingMode
	{
		None = 0,
		TriangleCountMetric = 1
	};

	struct FOptions
	{
		// number of requested LODs
		int32 NumLODs = 5;

		int32 NumCopiedLODs = 1;

		int32 ApproximationSourceLOD = 0;

		int32 NumSimplifiedLODs = 3;
		double SimplifyBaseTolerance = 1.0;
		double SimplifyLODLevelToleranceScale = 2.0;
		bool bSimplifyPreserveCorners = true;
		double SimplifySharpEdgeAngleDeg = 44.0;
		double SimplifyMinSalientDimension = 1.0;

		double OptimizeBaseTriCost = 0.7;
		double OptimizeLODLevelTriCostScale = 1.5;

		int32 NumVoxWrapLODs = 1;
		double VoxWrapBaseTolerance = 1.0;
		int32 VoxWrapMaxTriCountBase = 500;

		//
		// Hidden Faces removal options
		// 
		
		// overall strategy to use for removing hidden faces
		ERemoveHiddenFacesMode RemoveHiddenFacesMethod = ERemoveHiddenFacesMode::None;
		// start removing hidden faces at this LOD level 
		int32 RemoveHiddenStartLOD = 0;
		// (approximately) spacing between samples on triangle faces used for determining exterior visibility
		double RemoveHiddenSamplingDensity = 1.0;


		// LOD level to filter out detail parts
		int32 FilterDecorativePartsLODLevel = 2;
		// Decorative part will be approximated by simple shape for this many LOD levels before Filter level
		int ApproximateDecorativePartLODs = 1;

		// opening angle used to detect/assign sharp edges
		double HardNormalAngleDeg = 15.0;


		bool bMergeCoplanarFaces = true;
		int32 MergeCoplanarFacesStartLOD = 1;
		TFunction<UE::Geometry::FIndex3i(const UE::Geometry::FDynamicMesh3& Mesh, int32 TriangleID)> TriangleGroupingIDFunc;

		// If enabled, attempt to retriangulate planar areas of Source LODs to remove redundant coplanar geometry
		bool bRetriangulateSourceLODs = true;
		// which Source LOD to start retriangulating at
		int32 StartRetriangulateSourceLOD = 1;

		//
		// Optional Support for hitting explicit triangle counts for different LODs. 
		// The HardLODBudgets list should be provided in LOD order, ie first for Copied LODs,
		// then for Simplified LODs, then for Approximate LODs (Voxel LOD triangle counts are configured via VoxWrapMaxTriCountBase)
		// 
		// Currently the only explicit tri-count strategy in use is Part Promotion, where a coarser approximations
		// (eg Lower Copied LOD, Simplified LOD, Approximate LOD) are "promoted" upwards into higher combined-mesh LODs
		// as necessary to achieve the triangle count.
		// 
		// Note that final LOD triangle counts cannot be guaranteed, due to the combinatorial nature of the approximation.
		// For example the coarsest part LOD is a box with 12 triangles, so NumParts*12 is a lower bound on the initial combined mesh.
		// The Part Promotion strategy is applied *before* hidden removal and further mesh processing (eg coplanar merging),
		// so the final triangle count may be substantially lower than the budget (this is why the Multiplier is used below)
		//

		// list of fixed-triangle-count LOD budgets, in LOD order (ie LOD0, LOD1, LOD2, ...). If a triangle budgets are not specified for
		// a LOD, either by placing -1 in the array or truncating the array, that LOD will be left as-is. 
		TArray<int32> HardLODBudgets;
		// enable/disable the Part Promotion LOD strategy (described above)
		bool bEnableBudgetStrategy_PartLODPromotion = false;
		// Multiplier on LOD Budgets for PartLODPromotion strategy. This can be used to compensate for hidden-geometry removal and other optimizations done after the strategy is applied.
		double PartLODPromotionBudgetMultiplier = 2.0;


		//
		// Debug/utility options
		// 

		// Color mapping modes for vertex colors, primarily used for debugging
		EVertexColorMappingMode VertexColorMappingMode = EVertexColorMappingMode::None;
	};


	struct FOutputMesh
	{
		TArray<UE::Geometry::FDynamicMesh3> MeshLODs;
		TArray<UMaterialInterface*> MaterialSet;

		FKAggregateGeom SimpleCollisionShapes;
	};


	struct FResults
	{
		//EResultCode ResultCode = EResultCode::UnknownError;

		TArray<FOutputMesh> CombinedMeshes;
	};




	virtual FOptions ConstructDefaultOptions()
	{
		check(false);		// not implemented in base class
		return FOptions();
	}


	virtual void CombineMeshInstances(
		const FInstanceSet& MeshInstances, 
		const FOptions& Options, 
		FResults& ResultsOut) 
	{
		check(false);		// not implemented in base class
	}



	// Modular feature name to register for retrieval during runtime
	static const FName GetModularFeatureName()
	{
		return TEXT("GeometryProcessing_CombineMeshInstances");
	}

};
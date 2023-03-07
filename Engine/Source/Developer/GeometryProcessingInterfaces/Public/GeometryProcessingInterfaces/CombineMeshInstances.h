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

	struct FOptions
	{
		// number of requested LODs
		int32 NumLODs = 5;

		int32 NumCopiedLODs = 1;

		int32 ApproximationSourceLOD = 0;

		int32 NumSimplifiedLODs = 3;
		double SimplifyBaseTolerance = 1.0;
		double SimplifyLODLevelToleranceScale = 2.0;

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
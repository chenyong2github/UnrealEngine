// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "DynamicMeshAABBTree3.h"
#include "Sampling/MeshSurfaceSampler.h"
#include "Spatial/DenseGrid2.h"
#include "Image/ImageDimensions.h"
#include "Image/ImageOccupancyMap.h"

class DYNAMICMESH_API FMeshImageBakingCache
{
public:

	void SetDetailMesh(const FDynamicMesh3* Mesh, const FDynamicMeshAABBTree3* Spatial);
	void SetBakeTargetMesh(const FDynamicMesh3* Mesh);

	void SetDimensions(FImageDimensions Dimensions);
	void SetUVLayer(int32 UVLayer);
	void SetThickness(double Thickness);

	FImageDimensions GetDimensions() const { return Dimensions; }
	int32 GetUVLayer() const { return UVLayer; }
	double GetThickness() const { return Thickness; }

	const FDynamicMesh3* GetBakeTargetMesh() const { return TargetMesh; }
	const FDynamicMeshUVOverlay* GetBakeTargetUVs() const;
	const FDynamicMeshNormalOverlay* GetBakeTargetNormals() const;

	const FDynamicMesh3* GetDetailMesh() const { return DetailMesh; }
	const FDynamicMeshAABBTree3* GetDetailSpatial() const { return DetailSpatial; }
	const FDynamicMeshNormalOverlay* GetDetailNormals() const;


	bool IsCacheValid() const { return bOccupancyValid && bSamplesValid; }

	bool ValidateCache();


	struct FCorrespondenceSample
	{
		FMeshUVSampleInfo BaseSample;
		FVector3d BaseNormal;

		int32 DetailTriID;
		FVector3d DetailBaryCoords;
	};


	void EvaluateSamples(TFunctionRef<void(const FVector2i&, const FCorrespondenceSample&)> SampleFunction,
		bool bParallel = true) const;

	const FImageOccupancyMap* GetOccupancyMap() const;


protected:
	const FDynamicMesh3* DetailMesh = nullptr;
	const FDynamicMeshAABBTree3* DetailSpatial = nullptr;
	const FDynamicMesh3* TargetMesh = nullptr;

	FImageDimensions Dimensions;
	int32 UVLayer;
	double Thickness;

	TDenseGrid2<FCorrespondenceSample> SampleMap;
	bool bSamplesValid = false;
	void InvalidateSamples();


	TUniquePtr<FImageOccupancyMap> OccupancyMap;
	bool bOccupancyValid = false;
	void InvalidateOccupancy();

};


// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "DynamicMeshAABBTree3.h"
#include "MeshTangents.h"
#include "Sampling/MeshImageBaker.h"
#include "Sampling/MeshSurfaceSampler.h"
#include "Spatial/DenseGrid2.h"
#include "Image/ImageBuilder.h"
#include "Image/ImageDimensions.h"

namespace UE
{
namespace Geometry
{

class DYNAMICMESH_API FMeshMapBaker
{
public:

	//
	// Bake
	//

	/** Process all bakers to generate image results for each. */
	void Bake();

	/** Add a baker to be processed. */
	int32 AddBaker(TSharedPtr<FMeshImageBaker> Target);

	/** Reset the list of bakers. */
	void Reset();

	/** @return the bake result image for a given baker index. */
	const TSharedPtr<TImageBuilder<FVector4f>, ESPMode::ThreadSafe>& GetBakeResult(int32 Index) const { return BakeResults[Index]; }

	/** if this function returns true, we should abort calculation */
	TFunction<bool(void)> CancelF = []() { return false; };


	//
	// Parameters
	//

	/**
	 * ECorrespondenceStrategy determines the basic approach that will be used to establish a
	 * mapping from points on the BakeTarget Mesh (usually low-poly) to points on the Detail Mesh (eg highpoly).
	 * Geometrically this is not a 1-1 mapping so there are various options
	 */
	enum class ECorrespondenceStrategy
	{
		/** Raycast inwards from Point+Thickness*Normal, if that misses, try Outwards from Point, then Inwards from Point */
		RaycastStandard,
		/** Use geometrically nearest point. Thickness is ignored */
		NearestPoint,
		/** Use RaycastStandard but fall back to NearestPoint if none of the rays hit */
		RaycastStandardThenNearest,
		/** Assume that BakeTarget == DetailMesh and so no mapping is necessary */
		Identity
	};

	void SetDetailMesh(const FDynamicMesh3* Mesh, const FDynamicMeshAABBTree3* Spatial);
	void SetTargetMesh(const FDynamicMesh3* Mesh);

	void SetTargetMeshTangents(TSharedPtr<FMeshTangentsd, ESPMode::ThreadSafe> Tangents);

	const FDynamicMesh3* GetTargetMesh() const;
	const FDynamicMeshUVOverlay* GetTargetMeshUVs() const;
	const FDynamicMeshNormalOverlay* GetTargetMeshNormals() const;
	TSharedPtr<FMeshTangentsd, ESPMode::ThreadSafe> GetTargetMeshTangents() const;

	const FDynamicMesh3* GetDetailMesh() const;
	const FDynamicMeshAABBTree3* GetDetailMeshSpatial() const;
	const FDynamicMeshNormalOverlay* GetDetailMeshNormals() const;
	const FDynamicMeshUVOverlay* GetDetailMeshUVs(int32 UVLayer=0) const;

	void SetDimensions(FImageDimensions DimensionsIn);
	void SetUVLayer(int32 UVLayerIn);
	void SetThickness(double ThicknessIn);
	void SetGutterSize(int32 GutterSizeIn);
	void SetMultisampling(int32 MultisamplingIn);
	void SetCorrespondenceStrategy(ECorrespondenceStrategy Strategy);

	FImageDimensions GetDimensions() const { return Dimensions; }
	int32 GetUVLayer() const { return UVLayer; }
	double GetThickness() const { return Thickness; }
	int32 GetGutterSize() const { return GutterSize; }
	int32 GetMultisampling() const { return Multisampling; }
	ECorrespondenceStrategy GetCorrespondenceStrategy() const { return CorrespondenceStrategy; }

protected:
	void BakePixel(FImageOccupancyMap& OccupancyMap, const FImageDimensions& Tile, int32 ImgX, int32 ImgY);

protected:
	bool bParallel = true;

	const FDynamicMesh3* DetailMesh = nullptr;
	const FDynamicMeshAABBTree3* DetailSpatial = nullptr;
	const FDynamicMesh3* TargetMesh = nullptr;
	TSharedPtr<FMeshTangentsd, ESPMode::ThreadSafe> TargetMeshTangents;

	FDynamicMesh3 FlatMesh;
	TMeshSurfaceUVSampler<FMeshImageBaker::FCorrespondenceSample> DetailMeshSampler;

	FImageDimensions Dimensions = FImageDimensions(128, 128);
	int32 UVLayer = 0;
	double Thickness = 3.0;
	ECorrespondenceStrategy CorrespondenceStrategy = ECorrespondenceStrategy::RaycastStandard;
	int32 GutterSize = 4;
	int32 Multisampling = 1;

	const int32 TileSize = 32;

	TArray<TSharedPtr<FMeshImageBaker, ESPMode::ThreadSafe>> Bakers;
	TArray<TSharedPtr<TImageBuilder<FVector4f>, ESPMode::ThreadSafe>> BakeResults;
};

} // end namespace UE::Geometry
} // end namespace UE
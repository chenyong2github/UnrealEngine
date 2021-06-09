// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "DynamicMeshAABBTree3.h"
#include "MeshTangents.h"
#include "Sampling/MeshMapEvaluator.h"
#include "Sampling/MeshSurfaceSampler.h"
#include "Spatial/DenseGrid2.h"
#include "Image/ImageBuilder.h"
#include "Image/ImageDimensions.h"

namespace UE
{
namespace Geometry
{

class FImageOccupancyMap;

class FMeshMapTileBuffer
{
public:
	FMeshMapTileBuffer(const FImageDimensions& TileIn, const int32 PixelSizeIn)
		: Tile(TileIn)
		, PixelSize(PixelSizeIn + 1) // + 1 for accumulated pixel weight.
	{
		Buffer = static_cast<float*>(FMemory::MallocZeroed(sizeof(float) * PixelSize * Tile.Num()));
	}

	~FMeshMapTileBuffer()
	{
		FMemory::Free(Buffer);
	}

	float& GetPixelWeight(int64 LinearIdx)
	{
		checkSlow(LinearIdx >= 0 && LinearIdx < Tile.Num());
		return Buffer[LinearIdx * PixelSize];
	}

	float* GetPixel(int64 LinearIdx)
	{
		checkSlow(LinearIdx >= 0 && LinearIdx < Tile.Num());
		return &Buffer[LinearIdx * PixelSize + 1];
	}

private:
	const FImageDimensions Tile;
	const int32 PixelSize;
	float* Buffer;
};

class DYNAMICMESH_API FMeshMapBaker
{
public:

	//
	// Bake
	//

	/** Process all bakers to generate image results for each. */
	void Bake();

	/** Add a baker to be processed. */
	int32 AddBaker(TSharedPtr<FMeshMapEvaluator, ESPMode::ThreadSafe> Target);

	/** @return the evaluator at the given index. */
	FMeshMapEvaluator* GetBaker(int32 BakerIdx);

	/** @return the number of bake evaluators on this baker. */
	int32 NumBakers();

	/** Reset the list of bakers. */
	void Reset();

	/** @return the bake result image for a given baker index. */
	const TArrayView<TUniquePtr<TImageBuilder<FVector4f>>> GetBakeResults(int32 BakerIdx);

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
	/** Evaluate samples for this tile pixel. */
	void BakePixel(FMeshMapTileBuffer& TileBuffer, FImageOccupancyMap& OccupancyMap, const FImageDimensions& Tile, const FVector2i& TileCoords);

	/** Evaluate this sample. */
	void BakeSample(FMeshMapTileBuffer& TileBuffer, const FMeshMapEvaluator::FCorrespondenceSample& Sample, const FImageDimensions& Tile,
		const FVector2i& TileCoords, const FVector2i& ImageCoords, const float& SampleWeight);

	/** Initialize evaluation contexts and precompute data for bake evaluation. */
	void InitBake();

	/** Initialize bake sample default floats and colors. */
	void InitBakeDefaults();

	/**
	 * Convert float buffer value to float4 color. This function
	 * will advance the buffer pointer by the stride value.
	 * 
	 * @param Buffer [out] input buffer data.
	 * @param Stride the data layout of the buffer pointer.
	 * @param Weight the weight to apply to the buffer data.
	 */
	FVector4f FloatToPixel(float*& Buffer, FMeshMapEvaluator::EComponents Stride, float Weight);

protected:
	const bool bParallel = true;

	const FDynamicMesh3* DetailMesh = nullptr;
	const FDynamicMeshAABBTree3* DetailSpatial = nullptr;
	const FDynamicMesh3* TargetMesh = nullptr;
	TSharedPtr<FMeshTangentsd, ESPMode::ThreadSafe> TargetMeshTangents;

	FDynamicMesh3 FlatMesh;
	TMeshSurfaceUVSampler<FMeshMapEvaluator::FCorrespondenceSample> DetailMeshSampler;

	FImageDimensions Dimensions = FImageDimensions(128, 128);
	int32 UVLayer = 0;
	double Thickness = 3.0;
	ECorrespondenceStrategy CorrespondenceStrategy = ECorrespondenceStrategy::RaycastStandard;
	int32 GutterSize = 4;

	/** The square dimensions for multisampling each pixel. */
	int32 Multisampling = 1;

	/** The square dimensions for tiled processing of the output image(s). */
	const int32 TileSize = 32;

	/** The total size of the temporary float buffer for BakeSample. */
	int32 BakeSampleBufferSize = 0;

	/** The list of evaluators to process. */
	TArray<TSharedPtr<FMeshMapEvaluator, ESPMode::ThreadSafe>> Bakers;

	/** Evaluation contexts for each mesh evaluator. */
	TArray<FMeshMapEvaluator::FEvaluationContext> BakeContexts;

	/** Lists of Bake indices for each accumulation mode. */
	TArray<TArray<int32>> BakeAccumulateLists;

	/** Array of default values/colors per BakeResult. */
	TArray<float> BakeDefaults;
	TArray<FVector4f> BakeDefaultColors;
	
	/** Offsets per Baker into the BakeResults array.*/
	TArray<int32> BakeOffsets;

	/** Offsets per BakeResult into the BakeSample buffer.*/
	TArray<int32> BakeSampleOffsets;

	/** Array of bake result images. */
	TArray<TUniquePtr<TImageBuilder<FVector4f>>> BakeResults;
};

} // end namespace UE::Geometry
} // end namespace UE

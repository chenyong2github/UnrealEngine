// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sampling/MeshBaseBaker.h"
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
class FImageTile;
class FMeshMapTileBuffer;

class DYNAMICMESH_API FMeshMapBaker : public FMeshBaseBaker
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
	
	void SetDimensions(FImageDimensions DimensionsIn);
	void SetGutterSize(int32 GutterSizeIn);
	void SetMultisampling(int32 MultisamplingIn);

	FImageDimensions GetDimensions() const { return Dimensions; }
	int32 GetGutterSize() const { return GutterSize; }
	int32 GetMultisampling() const { return Multisampling; }

protected:
	/** Evaluate this sample. */
	void BakeSample(
		FMeshMapTileBuffer& TileBuffer,
		const FMeshMapEvaluator::FCorrespondenceSample& Sample,
		const FImageTile& Tile,
		const FVector2i& TileCoords,
		const FVector2i& ImageCoords,
		const float& SampleWeight);

	/** Initialize evaluation contexts and precompute data for bake evaluation. */
	void InitBake();

	/** Initialize bake sample default floats and colors. */
	void InitBakeDefaults();

protected:
	const bool bParallel = true;

	FDynamicMesh3 FlatMesh;
	TMeshSurfaceUVSampler<FMeshMapEvaluator::FCorrespondenceSample> DetailMeshSampler;

	FImageDimensions Dimensions = FImageDimensions(128, 128);
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

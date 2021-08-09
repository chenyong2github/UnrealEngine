// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sampling/MeshBaseBaker.h"
#include "Sampling/MeshMapEvaluator.h"
#include "Sampling/MeshSurfaceSampler.h"
#include "Image/ImageBuilder.h"
#include "Image/ImageDimensions.h"
#include "Image/BoxFilter.h"
#include "Image/BCSplineFilter.h"

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
	int32 NumBakers() const;

	/** Reset the list of bakers. */
	void Reset();

	/** @return the bake result image for a given baker index. */
	const TArrayView<TUniquePtr<TImageBuilder<FVector4f>>> GetBakeResults(int32 BakerIdx);

	/** if this function returns true, we should abort calculation */
	TFunction<bool(void)> CancelF = []() { return false; };


	//
	// Parameters
	//

	enum class EBakeFilterType
	{
		None,
		Box,
		BSpline,
		MitchellNetravali
	};
	
	void SetDimensions(FImageDimensions DimensionsIn);
	void SetGutterEnabled(bool bEnabled);
	void SetGutterSize(int32 GutterSizeIn);
	void SetMultisampling(int32 MultisamplingIn);
	void SetFilter(EBakeFilterType FilterTypeIn);

	FImageDimensions GetDimensions() const { return Dimensions; }
	bool GetGutterEnabled() const { return bGutterEnabled; }
	int32 GetGutterSize() const { return GutterSize; }
	int32 GetMultisampling() const { return Multisampling; }
	EBakeFilterType GetFilter() const { return FilterType; }

protected:
	/** Evaluate this sample. */
	void BakeSample(
		FMeshMapTileBuffer& TileBuffer,
		const FMeshMapEvaluator::FCorrespondenceSample& Sample,
		const FVector2d& UVPosition,
		const FVector2i& ImageCoords);

	/** Initialize evaluation contexts and precompute data for bake evaluation. */
	void InitBake();

	/** Initialize bake sample default floats and colors. */
	void InitBakeDefaults();

	/** Initialize filter */
	void InitFilter();

protected:
	const bool bParallel = true;

	FDynamicMesh3 FlatMesh;
	TMeshSurfaceUVSampler<FMeshMapEvaluator::FCorrespondenceSample> DetailMeshSampler;

	FImageDimensions Dimensions = FImageDimensions(128, 128);

	/**
	 * If true, the baker will pad the baked content past the UV borders by GutterSize.
	 * This is useful to minimize artifacts when filtering or mipmapping.
	 */
	bool bGutterEnabled = true;

	/** The pixel distance (in texel diagonal length) to pad baked content past the UV borders. */
	int32 GutterSize = 4;

	/** The square dimensions for multisampling each pixel. */
	int32 Multisampling = 1;

	/** The square dimensions for tiled processing of the output image(s). */
	const int32 TileSize = 32;

	/** The amount of padding for tiled processing of the output image(s). */
	const int32 TilePadding = 2;

	/** The pixel distance around the sample texel to be considered by the filter. [0, TilePadding] */
	int32 FilterKernelSize = 0;

	/** The texture filter type. */
	EBakeFilterType FilterType = EBakeFilterType::BSpline;

	/** Texture filters */
	static FBoxFilter BoxFilter;
	static FBSplineFilter BSplineFilter;
	static FMitchellNetravaliFilter MitchellNetravaliFilter;

	/** Texture filter function */
	using TextureFilterFn = float(*)(const FVector2d& Dist);
	TextureFilterFn TextureFilterEval = nullptr;

	template<EBakeFilterType BakeFilterType>
	static float EvaluateFilter(const FVector2d& Dist);

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

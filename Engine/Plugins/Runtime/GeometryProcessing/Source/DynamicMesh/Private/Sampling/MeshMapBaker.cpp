// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshMapBaker.h"
#include "Sampling/MeshBakerCommon.h"
#include "Image/ImageOccupancyMap.h"
#include "Image/ImageTile.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;


//
// FMeshMapBaker
//

static const float BoxFilterRadius = 0.5f;
static const float BCFilterRadius = 0.769f;

FBoxFilter FMeshMapBaker::BoxFilter(BoxFilterRadius);
FBSplineFilter FMeshMapBaker::BSplineFilter(BCFilterRadius);
FMitchellNetravaliFilter FMeshMapBaker::MitchellNetravaliFilter(BCFilterRadius);


void FMeshMapBaker::InitBake()
{
	// Retrieve evaluation contexts and cache:
	// - index lists of accumulation modes (BakeAccumulateLists)
	// - evaluator to bake result offsets (BakeOffsets)
	// - buffer size per sample (BakeSampleBufferSize)
	const int32 NumBakers = Bakers.Num();
	BakeContexts.SetNum(NumBakers);
	BakeOffsets.SetNumUninitialized(NumBakers + 1);
	BakeAccumulateLists.SetNum((int32) FMeshMapEvaluator::EAccumulateMode::Last);
	BakeSampleBufferSize = 0;
	int32 Offset = 0;
	for (int32 Idx = 0; Idx < NumBakers; ++Idx)
	{
		Bakers[Idx]->Setup(*this, BakeContexts[Idx]);
		checkSlow(BakeContexts[Idx].Evaluate != nullptr && BakeContexts[Idx].EvaluateDefault != nullptr);
		checkSlow(BakeContexts[Idx].DataLayout.Num() > 0);
		const int32 NumData = BakeContexts[Idx].DataLayout.Num();
		for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
		{
			BakeSampleBufferSize += (int32) BakeContexts[Idx].DataLayout[DataIdx];
		}
		BakeOffsets[Idx] = Offset;
		Offset += NumData;
		BakeAccumulateLists[(int32) BakeContexts[Idx].AccumulateMode].Add(Idx);
	}
	BakeOffsets[NumBakers] = Offset;

	// Initialize our BakeResults list and cache offsets into the sample buffer
	// per bake result
	const int32 NumResults = Offset;
	BakeResults.SetNum(NumResults);
	BakeSampleOffsets.SetNumUninitialized(NumResults + 1);
	int32 SampleOffset = 0;
	for (int32 Idx = 0; Idx < NumBakers; ++Idx)
	{
		int32 NumData = BakeContexts[Idx].DataLayout.Num();
		for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
		{
			int32 ResultIdx = BakeOffsets[Idx] + DataIdx;
			BakeResults[ResultIdx] = MakeUnique<TImageBuilder<FVector4f>>();
			BakeResults[ResultIdx]->SetDimensions(Dimensions);

			BakeSampleOffsets[ResultIdx] = SampleOffset;
			SampleOffset += (int32) BakeContexts[Idx].DataLayout[DataIdx];
		}
	}
	BakeSampleOffsets[NumResults] = SampleOffset;

	InitBakeDefaults();

	for (int32 Idx = 0; Idx < NumResults; ++Idx)
	{
		BakeResults[Idx]->Clear(BakeDefaultColors[Idx]);
	}

	InitFilter();
}

void FMeshMapBaker::InitBakeDefaults()
{
	// Cache default float buffer and colors for each bake result.
	checkSlow(BakeSampleBufferSize > 0);
	BakeDefaults.SetNumUninitialized(BakeSampleBufferSize);
	float* Buffer = BakeDefaults.GetData();
	float* BufferPtr = Buffer;

	const int32 NumBakers = Bakers.Num();
	for (int32 Idx = 0; Idx < NumBakers; ++Idx)
	{
		BakeContexts[Idx].EvaluateDefault(BufferPtr, BakeContexts[Idx].EvalData);
	}
	checkSlow((BufferPtr - Buffer) == BakeSampleBufferSize);

	BufferPtr = Buffer;
	const int32 NumBakeResults = BakeResults.Num();
	BakeDefaultColors.SetNumUninitialized(NumBakeResults);
	for (int32 Idx = 0; Idx < NumBakers; ++Idx)
	{
		const FMeshMapEvaluator::FEvaluationContext& Context = BakeContexts[Idx];
		const int32 NumData = Context.DataLayout.Num();
		for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
		{
			const int32 ResultIdx = BakeOffsets[Idx] + DataIdx;
			Context.EvaluateColor(DataIdx, BufferPtr, BakeDefaultColors[ResultIdx], Context.EvalData);
		}
	}
	checkSlow((BufferPtr - Buffer) == BakeSampleBufferSize);
}

void FMeshMapBaker::Bake()
{
	if (Bakers.IsEmpty())
	{
		return;
	}

	InitBake();

	// Generate UV space mesh
	const FDynamicMesh3* Mesh = TargetMesh;
	const FDynamicMeshUVOverlay* UVOverlay = GetTargetMeshUVs();
	const FDynamicMeshNormalOverlay* NormalOverlay = GetTargetMeshNormals();

	FlatMesh = FDynamicMesh3(EMeshComponents::FaceGroups);
	for (int32 tid : Mesh->TriangleIndicesItr())
	{
		if (UVOverlay->IsSetTriangle(tid))
		{
			FVector2f A, B, C;
			UVOverlay->GetTriElements(tid, A, B, C);
			const int32 VertA = FlatMesh.AppendVertex(FVector3d(A.X, A.Y, 0));
			const int32 VertB = FlatMesh.AppendVertex(FVector3d(B.X, B.Y, 0));
			const int32 VertC = FlatMesh.AppendVertex(FVector3d(C.X, C.Y, 0));
			/*int32 NewTriID =*/ FlatMesh.AppendTriangle(VertA, VertB, VertC, tid);
		}
	}

	ECorrespondenceStrategy UseStrategy = this->CorrespondenceStrategy;
	if (UseStrategy == ECorrespondenceStrategy::Identity && ensure(DetailMesh == Mesh) == false)
	{
		// Identity strategy requires mesh to be the same. Could potentially have two copies, in which
		// case this ensure is too conservative, but for now we will assume this
		UseStrategy = ECorrespondenceStrategy::NearestPoint;
	}

	// This sampler finds the correspondence between target surface and detail surface.
	DetailMeshSampler.Initialize(Mesh, UVOverlay, EMeshSurfaceSamplerQueryType::TriangleAndUV, FMeshMapEvaluator::FCorrespondenceSample(),
		[Mesh, NormalOverlay, UseStrategy, this](const FMeshUVSampleInfo& SampleInfo, FMeshMapEvaluator::FCorrespondenceSample& ValueOut)
	{
		NormalOverlay->GetTriBaryInterpolate<double>(SampleInfo.TriangleIndex, &SampleInfo.BaryCoords.X, &ValueOut.BaseNormal.X);
		Normalize(ValueOut.BaseNormal);
		FVector3d RayDir = ValueOut.BaseNormal;

		ValueOut.BaseSample = SampleInfo;
		ValueOut.DetailTriID = FDynamicMesh3::InvalidID;

		if (UseStrategy == ECorrespondenceStrategy::Identity)
		{
			ValueOut.DetailTriID = SampleInfo.TriangleIndex;
			ValueOut.DetailBaryCoords = SampleInfo.BaryCoords;
		}
		else if (UseStrategy == ECorrespondenceStrategy::NearestPoint)
		{
			bool bFoundTri = GetDetailMeshTrianglePoint_Nearest(*DetailMesh, *DetailSpatial, SampleInfo.SurfacePoint,
				ValueOut.DetailTriID, ValueOut.DetailBaryCoords);
		}
		else	// Fall back to raycast strategy
		{
			double SampleThickness = this->GetThickness();		// could modulate w/ a map here...

			// Find detail mesh triangle point
			bool bFoundTri = GetDetailMeshTrianglePoint_Raycast(*DetailMesh, *DetailSpatial, SampleInfo.SurfacePoint, RayDir,
				ValueOut.DetailTriID, ValueOut.DetailBaryCoords, SampleThickness,
				(UseStrategy == ECorrespondenceStrategy::RaycastStandardThenNearest));
		}
	});

	// Create a temporary output float buffer for the full image dimensions.
	const FImageTile ImageTile(FVector2i(0,0), FVector2i(Dimensions.GetWidth(), Dimensions.GetHeight()));
	FMeshMapTileBuffer ImageTileBuffer(ImageTile, BakeSampleBufferSize);

	// Tile the image
	FImageTiling Tiles(Dimensions, TileSize, TileSize);
	const int32 NumTiles = Tiles.Num();
	TArray<TArray64<TTuple<int64, int64>>> GutterTexelsPerTile;
	GutterTexelsPerTile.SetNum(NumTiles);
	ParallelFor(NumTiles, [this, &Tiles, &ImageTileBuffer, &GutterTexelsPerTile](int32 TileIdx)
	{
		// Generate unpadded and padded tiles.
		const FImageTile Tile = Tiles.GetTile(TileIdx);	// Image area to sample
		const FImageTile PaddedTile = Tiles.GetTile(TileIdx, TilePadding); // Filtered image area

		FImageOccupancyMap OccupancyMap;
		OccupancyMap.GutterSize = GutterSize;
		OccupancyMap.Initialize(Dimensions, Tile, Multisampling);
		OccupancyMap.ComputeFromUVSpaceMesh(FlatMesh, [this](int32 TriangleID) { return FlatMesh.GetTriangleGroup(TriangleID); });
		GutterTexelsPerTile[TileIdx] = OccupancyMap.GutterTexels;

		FMeshMapTileBuffer TileBuffer(PaddedTile, BakeSampleBufferSize);

		// Calculate interior texels
		const int TileWidth = Tile.GetWidth();
		const int TileHeight = Tile.GetHeight();
		const int32 NumSamples = OccupancyMap.Multisampler.Num();
		for (FVector2i TileCoords(0,0); TileCoords.Y < TileHeight; ++TileCoords.Y)
		{
			for (TileCoords.X = 0; TileCoords.X < TileWidth; ++TileCoords.X)
			{
				if (CancelF())
				{
					return;
				}

				const int64 TilePixelLinearIdx = Tile.GetIndex(TileCoords);
				if (OccupancyMap.TexelNumSamples(TilePixelLinearIdx) == 0)
				{
					continue;
				}

				const FVector2i ImageCoords = Tile.GetSourceCoords(TileCoords);
				for (int32 SampleIdx = 0; SampleIdx < NumSamples; ++SampleIdx)
				{
					const int64 LinearIdx = TilePixelLinearIdx * NumSamples + SampleIdx;
					if (OccupancyMap.IsInterior(LinearIdx))
					{
						const FVector2d UVPosition = (FVector2d)OccupancyMap.TexelQueryUV[LinearIdx];
						const int32 UVTriangleID = OccupancyMap.TexelQueryTriangle[LinearIdx];

						FMeshMapEvaluator::FCorrespondenceSample Sample;
						DetailMeshSampler.SampleUV(UVTriangleID, UVPosition, Sample);

						BakeSample(TileBuffer, Sample, UVPosition, ImageCoords);
					}
				}
			}
		}

		auto NoopFn = [](const float& In, float& Out)
		{
		};

		auto AddFn = [](const float& In, float& Out)
		{
			Out += In;
		};

		auto OverwriteFn = [](const float& In, float& Out)
		{
			Out = In;
		};

		// WriteToOutputBuffer transfers local tile data (TileBuffer) to the image output buffer (ImageTileBuffer).
		auto WriteToOutputBuffer = [this, &TileBuffer, &ImageTileBuffer] (const FImageTile& TargetTile, const TArray<int32>& BakeIds, auto&& Op, auto&& WeightOp)
		{
			const int TargetTileWidth = TargetTile.GetWidth();
			const int TargetTileHeight = TargetTile.GetHeight();
			for (FVector2i TileCoords(0,0); TileCoords.Y < TargetTileHeight; ++TileCoords.Y)
			{
				for (TileCoords.X = 0; TileCoords.X < TargetTileWidth; ++TileCoords.X)
				{
					if (CancelF())
					{
						return;
					}

					const FVector2i ImageCoords = TargetTile.GetSourceCoords(TileCoords);

					const FImageTile& BufferTile = TileBuffer.GetTile();
					const int64 TilePixelLinearIdx = BufferTile.GetIndexFromSourceCoords(ImageCoords);
					const float& TilePixelWeight = TileBuffer.GetPixelWeight(TilePixelLinearIdx);
					float* TilePixelBuffer = TileBuffer.GetPixel(TilePixelLinearIdx);

					const int64 ImageLinearIdx = Dimensions.GetIndex(ImageCoords);
					float& ImagePixelWeight = ImageTileBuffer.GetPixelWeight(ImageLinearIdx);
					float* ImagePixelBuffer = ImageTileBuffer.GetPixel(ImageLinearIdx);

					WeightOp(TilePixelWeight, ImagePixelWeight);
					for( int32 Idx : BakeIds )
					{
						const FMeshMapEvaluator::FEvaluationContext& Context = BakeContexts[Idx];
						const int32 NumData = Context.DataLayout.Num();
						const int32 ResultOffset = BakeOffsets[Idx];
						for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
						{
							const int32 ResultIdx = ResultOffset + DataIdx;
							const int32 Offset = BakeSampleOffsets[ResultIdx];
							float* BufferPtr = &TilePixelBuffer[Offset];
							float* ImageBufferPtr = &ImagePixelBuffer[Offset];
							const FMeshMapEvaluator::EComponents Stride = Context.DataLayout[DataIdx];

							for (int32 FloatIdx = 0; FloatIdx < (int32)Stride; ++FloatIdx)
							{
								Op(BufferPtr[FloatIdx], ImageBufferPtr[FloatIdx]);
							}
						}
					}
				}
			}
		};

		// Transfer 'Overwrite' float data to image tile buffer
		WriteToOutputBuffer(Tile, BakeAccumulateLists[(int32)FMeshMapEvaluator::EAccumulateMode::Overwrite], OverwriteFn, NoopFn);

		// Accumulate 'Add' float data to image tile buffer
		// TODO: Optimize this write lock
		FCriticalSection WriteLock;
		WriteToOutputBuffer(PaddedTile, BakeAccumulateLists[(int32)FMeshMapEvaluator::EAccumulateMode::Add], AddFn, AddFn);
	}, !bParallel ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	// Normalize and convert ImageTileBuffer data to color data.
	ParallelFor(NumTiles, [this, &Tiles, &ImageTileBuffer](int32 TileIdx)
	{
		const FImageTile Tile = Tiles.GetTile(TileIdx);
		const int TileWidth = Tile.GetWidth();
		const int TileHeight = Tile.GetHeight();
		for (FVector2i TileCoords(0,0); TileCoords.Y < TileHeight; ++TileCoords.Y)
		{
			for (TileCoords.X = 0; TileCoords.X < TileWidth; ++TileCoords.X)
			{
				if (CancelF())
				{
					return;
				}

				const FVector2i ImageCoords = Tile.GetSourceCoords(TileCoords);
				const int64 ImageLinearIdx = Dimensions.GetIndex(ImageCoords);
				const float& PixelWeight = ImageTileBuffer.GetPixelWeight(ImageLinearIdx);
				float* PixelBuffer = ImageTileBuffer.GetPixel(ImageLinearIdx);

				auto WriteToPixel = [this, &PixelBuffer, &ImageLinearIdx](TArray<int32>& BakeIds, float OneOverWeight)
				{
					for (int32 Idx : BakeIds)
					{
						const FMeshMapEvaluator::FEvaluationContext& Context = BakeContexts[Idx];
						const int32 NumData = Context.DataLayout.Num();
						const int32 ResultOffset = BakeOffsets[Idx];
						for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
						{
							const int32 ResultIdx = ResultOffset + DataIdx;
							const int32 Offset = BakeSampleOffsets[ResultIdx];
							float* BufferPtr = &PixelBuffer[Offset];
							const FMeshMapEvaluator::EComponents Stride = Context.DataLayout[DataIdx];

							// Apply weight to raw float data.
							for (int32 FloatIdx = 0; FloatIdx < (int32)Stride; ++FloatIdx)
							{
								BufferPtr[FloatIdx] *= OneOverWeight;
							}

							// Convert float data to color.
							FVector4f& Pixel = BakeResults[ResultIdx]->GetPixel(ImageLinearIdx);
							Context.EvaluateColor(DataIdx, BufferPtr, Pixel, Context.EvalData);
						}
					}
				};
				
				if (PixelWeight > 0.0)
				{
					WriteToPixel(BakeAccumulateLists[(int32)FMeshMapEvaluator::EAccumulateMode::Add], 1.0f / PixelWeight);
				}
				WriteToPixel(BakeAccumulateLists[(int32)FMeshMapEvaluator::EAccumulateMode::Overwrite], 1.0f);
			}
		}
	}, !bParallel ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	// Gutter Texel processing
	if( bGutterEnabled )
	{
		const int32 NumResults = BakeResults.Num();
		ParallelFor(NumTiles, [this, &NumResults, &GutterTexelsPerTile](int32 TileIdx)
		{
			for (int64 GutterIdx = 0; GutterIdx < GutterTexelsPerTile[TileIdx].Num(); ++GutterIdx)
			{
				int64 GutterPixelTo;
				int64 GutterPixelFrom;
				Tie(GutterPixelTo, GutterPixelFrom) = GutterTexelsPerTile[TileIdx][GutterIdx];
				FVector2i FromCoords = Dimensions.GetCoords(GutterPixelFrom);
				FVector2i ToCoords = Dimensions.GetCoords(GutterPixelTo);
				for (int32 Idx = 0; Idx < NumResults; Idx++)
				{
					BakeResults[Idx]->CopyPixel(GutterPixelFrom, GutterPixelTo);
				}
			}
		}, !bParallel ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
	}
}

void FMeshMapBaker::BakeSample(
	FMeshMapTileBuffer& TileBuffer,
	const FMeshMapEvaluator::FCorrespondenceSample& Sample,
	const FVector2d& UVPosition,
	const FVector2i& ImageCoords)
{
	// Evaluate each baker into stack allocated float buffer
	float* Buffer = static_cast<float*>(FMemory_Alloca(sizeof(float) * BakeSampleBufferSize));
	float* BufferPtr = Buffer;
	const int32 NumBakers = Bakers.Num();
	for (int32 Idx = 0; Idx < NumBakers; ++Idx)
	{
		BakeContexts[Idx].Evaluate(BufferPtr, Sample, BakeContexts[Idx].EvalData);
	}
	checkSlow((BufferPtr - Buffer) == BakeSampleBufferSize);

	const FImageTile& Tile = TileBuffer.GetTile();

	auto AddFn = [this, &ImageCoords, &UVPosition, &Tile, &TileBuffer, Buffer](const TArray<int32>& BakeIds) -> void
	{
		const FVector2i BoxFilterStart(
			FMath::Clamp(ImageCoords.X - FilterKernelSize, 0, Dimensions.GetWidth()),
			FMath::Clamp(ImageCoords.Y - FilterKernelSize, 0, Dimensions.GetHeight())
			);
		const FVector2i BoxFilterEnd(
			FMath::Clamp(ImageCoords.X + FilterKernelSize + 1, 0, Dimensions.GetWidth()),
			FMath::Clamp(ImageCoords.Y + FilterKernelSize + 1, 0, Dimensions.GetHeight())
			);
		const FImageTile BoxFilterTile(BoxFilterStart, BoxFilterEnd);

		for (int64 FilterIdx = 0; FilterIdx < BoxFilterTile.Num(); FilterIdx++)
		{
			const FVector2i SourceCoords = BoxFilterTile.GetSourceCoords(FilterIdx);
			const int64 BufferTilePixelLinearIdx = Tile.GetIndexFromSourceCoords(SourceCoords);
			float* PixelBuffer = TileBuffer.GetPixel(BufferTilePixelLinearIdx);
			float& PixelWeight = TileBuffer.GetPixelWeight(BufferTilePixelLinearIdx);

			// Apply filter using double linear weighting (once per axis)
			FVector2d TexelDistance = Dimensions.GetTexelUV(SourceCoords) - UVPosition;
			TexelDistance.X *= Dimensions.GetWidth();
			TexelDistance.Y *= Dimensions.GetHeight();

			const float FilterWeight = TextureFilterEval(TexelDistance);
			PixelWeight += FilterWeight;
			for (int32 BakeIdx : BakeIds)
			{
				const FMeshMapEvaluator::FEvaluationContext& Context = BakeContexts[BakeIdx];
				const int32 NumData = Context.DataLayout.Num();
				const int32 ResultOffset = BakeOffsets[BakeIdx];
				for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
				{
					const int32 ResultIdx = ResultOffset + DataIdx;
					const int32 Offset = BakeSampleOffsets[ResultIdx];
					const int32 Stride = (int32) Context.DataLayout[DataIdx];
					for (int32 BufIdx = Offset; BufIdx < Offset + Stride; ++BufIdx)
					{
						PixelBuffer[BufIdx] += Buffer[BufIdx] * FilterWeight;
					}
				}
			}
		}
	};

	auto OverwriteFn = [this, &ImageCoords, &Tile, &TileBuffer, Buffer](const TArray<int32>& BakeIds) -> void
	{
		const int64 BufferTilePixelLinearIdx = Tile.GetIndexFromSourceCoords(ImageCoords); 
		float* PixelBuffer = TileBuffer.GetPixel(BufferTilePixelLinearIdx);
		
		for (int32 Idx : BakeIds)
		{
			const FMeshMapEvaluator::FEvaluationContext& Context = BakeContexts[Idx];
			const int32 NumData = Context.DataLayout.Num();
			const int32 ResultOffset = BakeOffsets[Idx];
			for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
			{
				const int32 ResultIdx = ResultOffset + DataIdx;
				const int32 Offset = BakeSampleOffsets[ResultIdx];
				const int32 Stride = (int32) Context.DataLayout[DataIdx];
				for (int32 BufIdx = Offset; BufIdx < Offset + Stride; ++BufIdx)
				{
					PixelBuffer[BufIdx] = Buffer[BufIdx];
				}
			}
		}
	};

	AddFn(BakeAccumulateLists[(int32)FMeshMapEvaluator::EAccumulateMode::Add]);
	OverwriteFn(BakeAccumulateLists[(int32)FMeshMapEvaluator::EAccumulateMode::Overwrite]);
}

int32 FMeshMapBaker::AddBaker(TSharedPtr<FMeshMapEvaluator> Sampler)
{
	return Bakers.Add(Sampler);
}

FMeshMapEvaluator* FMeshMapBaker::GetBaker(const int32 BakerIdx)
{
	return Bakers[BakerIdx].Get();
}

void FMeshMapBaker::Reset()
{
	Bakers.Empty();
	BakeResults.Empty();
}

int32 FMeshMapBaker::NumBakers() const
{
	return Bakers.Num();
}

const TArrayView<TUniquePtr<TImageBuilder<FVector4f>>> FMeshMapBaker::GetBakeResults(int32 BakerIdx)
{
	const int32 ResultIdx = BakeOffsets[BakerIdx];
	const int32 NumResults = BakeOffsets[BakerIdx + 1] - ResultIdx;
	return TArrayView<TUniquePtr<TImageBuilder<FVector4f>>>(&BakeResults[ResultIdx], NumResults);
}

void FMeshMapBaker::SetDimensions(FImageDimensions DimensionsIn)
{
	Dimensions = DimensionsIn;
}

void FMeshMapBaker::SetGutterEnabled(bool bEnabled)
{
	bGutterEnabled = bEnabled;
}

void FMeshMapBaker::SetGutterSize(int32 GutterSizeIn)
{
	// GutterSize must be >= 1 since it is tied to MaxDistance for the
	// OccupancyMap spatial search.
	GutterSize = GutterSizeIn >= 1 ? GutterSizeIn : 1;
}

void FMeshMapBaker::SetMultisampling(int32 MultisamplingIn)
{
	Multisampling = MultisamplingIn;
}

void FMeshMapBaker::SetFilter(EBakeFilterType FilterTypeIn)
{
	FilterType = FilterTypeIn;
}

void FMeshMapBaker::InitFilter()
{
	FilterKernelSize = TilePadding;
	switch(FilterType)
	{
	case EBakeFilterType::None:
		FilterKernelSize = 0;
		TextureFilterEval = &EvaluateFilter<EBakeFilterType::None>;
		break;
	case EBakeFilterType::Box:
		TextureFilterEval = &EvaluateFilter<EBakeFilterType::Box>;
		break;
	case EBakeFilterType::BSpline:
		TextureFilterEval = &EvaluateFilter<EBakeFilterType::BSpline>;
		break;
	case EBakeFilterType::MitchellNetravali:
		TextureFilterEval = &EvaluateFilter<EBakeFilterType::MitchellNetravali>;
		break;
	}
}

template<FMeshMapBaker::EBakeFilterType BakeFilterType>
float FMeshMapBaker::EvaluateFilter(const FVector2d& Dist)
{
	float Result = 0.0f;
	if constexpr(BakeFilterType == EBakeFilterType::None)
	{
		Result = 1.0f;
	}
	else if constexpr(BakeFilterType == EBakeFilterType::Box)
	{
		Result = BoxFilter.GetWeight(Dist);
	}
	else if constexpr(BakeFilterType == EBakeFilterType::BSpline)
	{
		Result = BSplineFilter.GetWeight(Dist);
	}
	else if constexpr(BakeFilterType == EBakeFilterType::MitchellNetravali)
	{
		Result = MitchellNetravaliFilter.GetWeight(Dist);
	}
	return Result;
}




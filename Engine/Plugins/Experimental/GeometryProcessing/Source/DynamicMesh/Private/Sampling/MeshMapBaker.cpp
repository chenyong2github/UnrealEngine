// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshMapBaker.h"
#include "Sampling/MeshBakerCommon.h"
#include "Image/ImageOccupancyMap.h"
#include "Image/ImageTile.h"
#include "Spatial/DenseGrid2.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;


//
// FMeshMapBaker
//


void FMeshMapBaker::InitBake()
{
	// Retrieve evaluation contexts and cache:
	// - index lists of accumulation modes (BakeAccumulateLists)
	// - evaluator to bake result offsets (BakeOffsets)
	// - buffer size per sample (BakeSampleBufferSize)
	int32 NumBakers = Bakers.Num();
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
		int32 NumData = BakeContexts[Idx].DataLayout.Num();
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
			int32 VertA = FlatMesh.AppendVertex(FVector3d(A.X, A.Y, 0));
			int32 VertB = FlatMesh.AppendVertex(FVector3d(B.X, B.Y, 0));
			int32 VertC = FlatMesh.AppendVertex(FVector3d(C.X, C.Y, 0));
			int32 NewTriID = FlatMesh.AppendTriangle(VertA, VertB, VertC, tid);
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

	// Setup image tiling
	FImageTiling Tiles(Dimensions, TileSize, TileSize);
	const int32 NumBakers = Bakers.Num();
	const int32 NumResults = BakeResults.Num();
	ParallelFor(Tiles.Num(), [this, &Tiles, &NumBakers, &NumResults](int32 TileIdx)
	{
		const FImageTile Tile = Tiles.GetTile(TileIdx);

		FImageOccupancyMap OccupancyMap;
		OccupancyMap.GutterSize = GutterSize;
		OccupancyMap.Initialize(Dimensions, Tile, Multisampling);
		OccupancyMap.ComputeFromUVSpaceMesh(FlatMesh, [this](int32 TriangleID) { return FlatMesh.GetTriangleGroup(TriangleID); });

		FMeshMapTileBuffer TileBuffer(Tile, BakeSampleBufferSize);

		// Calculate interior texels
		for (FVector2i TileCoords(0,0); TileCoords.Y < Tile.GetHeight(); ++TileCoords.Y)
		{
			for (TileCoords.X = 0; TileCoords.X < Tile.GetWidth(); ++TileCoords.X)
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
				const int32 NumSamples = OccupancyMap.Multisampler.Num();
				for (int32 SampleIdx = 0; SampleIdx < NumSamples; ++SampleIdx)
				{
					const int64 LinearIdx = TilePixelLinearIdx * OccupancyMap.Multisampler.Num() + SampleIdx;
					if (OccupancyMap.IsInterior(LinearIdx))
					{
						const FVector2d UVPosition = (FVector2d)OccupancyMap.TexelQueryUV[LinearIdx];
						const int32 UVTriangleID = OccupancyMap.TexelQueryTriangle[LinearIdx];

						FMeshMapEvaluator::FCorrespondenceSample Sample;
						DetailMeshSampler.SampleUV(UVTriangleID, UVPosition, Sample);

						BakeSample(TileBuffer, Sample, Tile, TileCoords, ImageCoords, 1.0f);
					}
				}
			}
		}

		// Convert TileBuffer data to color data.
		for (FVector2i Texel(0,0); Texel.Y < Tile.GetHeight(); ++Texel.Y)
		{
			for (Texel.X = 0; Texel.X < Tile.GetWidth(); ++Texel.X)
			{
				if (CancelF())
				{
					return;
				}

				const int64 PixelLinearIdx = Tile.GetIndex(Texel);
				const float& PixelWeight = TileBuffer.GetPixelWeight(PixelLinearIdx);
				if (PixelWeight > 0.0)
				{
					const FVector2i ImageCoords = Tile.GetSourceCoords(Texel);
					const int64 ImageLinearIdx = Dimensions.GetIndex(ImageCoords);
					float* PixelBuffer = TileBuffer.GetPixel(PixelLinearIdx);
					const float OneOverPixelWeight = 1.0f / PixelWeight;

					auto WriteToPixel = [this, &PixelBuffer, &ImageLinearIdx](TArray<int32>& BakeIds, float Weight)
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
									BufferPtr[FloatIdx] *= Weight;
								}

								// Convert float data to color.
								FVector4f& Pixel = BakeResults[ResultIdx]->GetPixel(ImageLinearIdx);
								Context.EvaluateColor(DataIdx, BufferPtr, Pixel, Context.EvalData);
							}
						}
					};

					WriteToPixel(BakeAccumulateLists[(int32)FMeshMapEvaluator::EAccumulateMode::Add], OneOverPixelWeight);
					WriteToPixel(BakeAccumulateLists[(int32)FMeshMapEvaluator::EAccumulateMode::Overwrite], 1.0f);
				}
			}
		}

		for (int64 GutterIdx = 0; GutterIdx < OccupancyMap.GutterTexels.Num(); ++GutterIdx)
		{
			int64 GutterPixelTo;
			int64 GutterPixelFrom;
			Tie(GutterPixelTo, GutterPixelFrom) = OccupancyMap.GutterTexels[GutterIdx];
			for (int32 Idx = 0; Idx < NumResults; Idx++)
			{
				BakeResults[Idx]->CopyPixel(GutterPixelFrom, GutterPixelTo);
			}
		}
	}, !bParallel ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
}

void FMeshMapBaker::BakeSample(FMeshMapTileBuffer& TileBuffer, const FMeshMapEvaluator::FCorrespondenceSample& Sample, const FImageTile& Tile,
	const FVector2i& TileCoords, const FVector2i& ImageCoords, const float& SampleWeight)
{
	const int64 TilePixelLinearIdx = Tile.GetIndex(TileCoords);
	float* PixelBuffer = TileBuffer.GetPixel(TilePixelLinearIdx);
	float& PixelWeight = TileBuffer.GetPixelWeight(TilePixelLinearIdx);
	PixelWeight += SampleWeight;

	float* Buffer = static_cast<float*>(FMemory_Alloca(sizeof(float) * BakeSampleBufferSize));
	float* BufferPtr = Buffer;
	const int32 NumBakers = Bakers.Num();
	for (int32 Idx = 0; Idx < NumBakers; ++Idx)
	{
		BakeContexts[Idx].Evaluate(BufferPtr, Sample, BakeContexts[Idx].EvalData);
	}
	checkSlow((BufferPtr - Buffer) == BakeSampleBufferSize);

	auto AddFn = [PixelBuffer, Buffer](const int32 Idx) -> void
	{
		PixelBuffer[Idx] += Buffer[Idx];
	};

	auto OverwriteFn = [PixelBuffer, Buffer](const int32 Idx) -> void
	{
		PixelBuffer[Idx] = Buffer[Idx];
	};

	auto AccumulateFn = [this](const TArray<int32>& BakeIds, auto&& Op)
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
				const int32 Stride = (int32) Context.DataLayout[DataIdx];
				for (int32 BufIdx = Offset; BufIdx < Offset + Stride; ++BufIdx)
				{
					Op(BufIdx);
				}
			}
		}
	};

	AccumulateFn(BakeAccumulateLists[(int32)FMeshMapEvaluator::EAccumulateMode::Add], AddFn);
	AccumulateFn(BakeAccumulateLists[(int32)FMeshMapEvaluator::EAccumulateMode::Overwrite], OverwriteFn);
}

int32 FMeshMapBaker::AddBaker(TSharedPtr<FMeshMapEvaluator> Sampler)
{
	return Bakers.Add(Sampler);
}

FMeshMapEvaluator* FMeshMapBaker::GetBaker(int32 BakerIdx)
{
	return Bakers[BakerIdx].Get();
}

void FMeshMapBaker::Reset()
{
	Bakers.Empty();
	BakeResults.Empty();
}

int32 FMeshMapBaker::NumBakers()
{
	return Bakers.Num();
}

const TArrayView<TUniquePtr<TImageBuilder<FVector4f>>> FMeshMapBaker::GetBakeResults(int32 BakerIdx)
{
	int32 ResultIdx = BakeOffsets[BakerIdx];
	int32 NumResults = BakeOffsets[BakerIdx + 1] - ResultIdx;
	return TArrayView<TUniquePtr<TImageBuilder<FVector4f>>>(&BakeResults[ResultIdx], NumResults);
}

void FMeshMapBaker::SetDimensions(FImageDimensions DimensionsIn)
{
	Dimensions = DimensionsIn;
}

void FMeshMapBaker::SetGutterSize(int32 GutterSizeIn)
{
	GutterSize = GutterSizeIn;
}

void FMeshMapBaker::SetMultisampling(int32 MultisamplingIn)
{
	Multisampling = MultisamplingIn;
}




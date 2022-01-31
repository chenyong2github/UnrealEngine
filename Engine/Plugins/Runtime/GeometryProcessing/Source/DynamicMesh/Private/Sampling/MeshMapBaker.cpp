// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshMapBaker.h"
#include "Sampling/MeshBakerCommon.h"
#include "Sampling/MeshMapBakerQueue.h"
#include "Image/ImageOccupancyMap.h"
#include "Image/ImageTile.h"
#include "Selections/MeshConnectedComponents.h"
#include "ProfilingDebugging/ScopedTimers.h"

using namespace UE::Geometry;

//
// FMeshMapBaker
//

static constexpr float BoxFilterRadius = 0.5f;
static constexpr float BCFilterRadius = 0.769f;

FBoxFilter FMeshMapBaker::BoxFilter(BoxFilterRadius);
FBSplineFilter FMeshMapBaker::BSplineFilter(BCFilterRadius);
FMitchellNetravaliFilter FMeshMapBaker::MitchellNetravaliFilter(BCFilterRadius);


void FMeshMapBaker::InitBake()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMapBaker::InitBake);
	
	// Retrieve evaluation contexts and cache:
	// - index lists of accumulation modes (BakeAccumulateLists)
	// - evaluator to bake result offsets (BakeOffsets)
	// - buffer size per sample (BakeSampleBufferSize)
	const int32 NumBakers = Bakers.Num();
	BakeContexts.SetNum(NumBakers);
	BakeOffsets.SetNumUninitialized(NumBakers + 1);
	BakeAccumulateLists.SetNum(static_cast<int32>(FMeshMapEvaluator::EAccumulateMode::Last));
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
			BakeSampleBufferSize += static_cast<int32>(BakeContexts[Idx].DataLayout[DataIdx]);
		}
		BakeOffsets[Idx] = Offset;
		Offset += NumData;
		BakeAccumulateLists[static_cast<int32>(BakeContexts[Idx].AccumulateMode)].Add(Idx);
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
		const int32 NumData = BakeContexts[Idx].DataLayout.Num();
		for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
		{
			const int32 ResultIdx = BakeOffsets[Idx] + DataIdx;
			BakeResults[ResultIdx] = MakeUnique<TImageBuilder<FVector4f>>();
			BakeResults[ResultIdx]->SetDimensions(Dimensions);

			BakeSampleOffsets[ResultIdx] = SampleOffset;
			SampleOffset += static_cast<int32>(BakeContexts[Idx].DataLayout[DataIdx]);
		}
	}
	BakeSampleOffsets[NumResults] = SampleOffset;

	InitBakeDefaults();

	for (int32 Idx = 0; Idx < NumResults; ++Idx)
	{
		BakeResults[Idx]->Clear(BakeDefaultColors[Idx]);
	}

	InitFilter();

	// Compute UV charts if null or invalid.
	if (!TargetMeshUVCharts || !ensure(TargetMeshUVCharts->Num() == TargetMesh->TriangleCount()))
	{
		ComputeUVCharts(*TargetMesh, TargetMeshUVChartsLocal);
		TargetMeshUVCharts = &TargetMeshUVChartsLocal;
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
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMapBaker::Bake);

	BakeAnalytics.Reset();
	FScopedDurationTimer TotalBakeTimer(BakeAnalytics.TotalBakeDuration);
	
	if (Bakers.IsEmpty() || !TargetMesh)
	{
		return;
	}

	InitBake();

	const FDynamicMesh3* Mesh = TargetMesh;
	const FDynamicMeshUVOverlay* UVOverlay = GetTargetMeshUVs();
	const FDynamicMeshNormalOverlay* NormalOverlay = GetTargetMeshNormals();

	{
		// Generate UV space mesh
		TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMapBaker::Bake_CreateUVMesh);
		
		FlatMesh = FDynamicMesh3(EMeshComponents::FaceGroups);
		for (const int32 TriId : Mesh->TriangleIndicesItr())
		{
			if (UVOverlay->IsSetTriangle(TriId))
			{
				FVector2f A, B, C;
				UVOverlay->GetTriElements(TriId, A, B, C);
				const int32 VertA = FlatMesh.AppendVertex(FVector3d(A.X, A.Y, 0));
				const int32 VertB = FlatMesh.AppendVertex(FVector3d(B.X, B.Y, 0));
				const int32 VertC = FlatMesh.AppendVertex(FVector3d(C.X, C.Y, 0));
				/*int32 NewTriID =*/ FlatMesh.AppendTriangle(VertA, VertB, VertC, TriId);
			}
		}
	}

	ECorrespondenceStrategy UseStrategy = this->CorrespondenceStrategy;
	bool bIsIdentity = true;
	int NumDetailMeshes = 0;
	auto CheckIdentity = [Mesh, &bIsIdentity, &NumDetailMeshes](const void* DetailMesh)
	{
		bIsIdentity = bIsIdentity && (DetailMesh == Mesh);
		++NumDetailMeshes;
	};
	DetailSampler->ProcessMeshes(CheckIdentity);
	if (UseStrategy == ECorrespondenceStrategy::Identity && !ensure(bIsIdentity && (NumDetailMeshes == 1)))
	{
		// Identity strategy requires mesh to be the same. Could potentially have two copies, in which
		// case this ensure is too conservative, but for now we will assume this
		UseStrategy = ECorrespondenceStrategy::NearestPoint;
	}

	// This sampler finds the correspondence between target surface and detail surface.
	DetailCorrespondenceSampler.Initialize(Mesh, UVOverlay, EMeshSurfaceSamplerQueryType::TriangleAndUV, FMeshMapEvaluator::FCorrespondenceSample(),
		[Mesh, NormalOverlay, UseStrategy, this](const FMeshUVSampleInfo& SampleInfo, FMeshMapEvaluator::FCorrespondenceSample& ValueOut)
	{
		NormalOverlay->GetTriBaryInterpolate<double>(SampleInfo.TriangleIndex, &SampleInfo.BaryCoords.X, &ValueOut.BaseNormal.X);
		Normalize(ValueOut.BaseNormal);
		const FVector3d RayDir = ValueOut.BaseNormal;

		ValueOut.BaseSample = SampleInfo;
		ValueOut.DetailMesh = nullptr;
		ValueOut.DetailTriID = FDynamicMesh3::InvalidID;

		if (UseStrategy == ECorrespondenceStrategy::Identity && DetailSampler->SupportsIdentityCorrespondence())
		{
			ValueOut.DetailMesh = Mesh;
			ValueOut.DetailTriID = SampleInfo.TriangleIndex;
			ValueOut.DetailBaryCoords = SampleInfo.BaryCoords;
		}
		else if (UseStrategy == ECorrespondenceStrategy::NearestPoint && DetailSampler->SupportsNearestPointCorrespondence())
		{
			ValueOut.DetailMesh = GetDetailMeshTrianglePoint_Nearest(DetailSampler, SampleInfo.SurfacePoint,
				ValueOut.DetailTriID, ValueOut.DetailBaryCoords);
		}
		else	// Fall back to raycast strategy
		{
			checkSlow(DetailSampler->SupportsRaycastCorrespondence());
			
			const double SampleThickness = this->GetProjectionDistance();		// could modulate w/ a map here...

			// Find detail mesh triangle point
			ValueOut.DetailMesh = GetDetailMeshTrianglePoint_Raycast(DetailSampler, SampleInfo.SurfacePoint, RayDir,
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
	auto WriteToOutputBuffer = [this, &ImageTileBuffer] (FMeshMapTileBuffer& TileBufferIn, const FImageTile& TargetTile, const TArray<int32>& BakeIds, auto&& Op, auto&& WeightOp)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMapBaker::Bake_WriteToOutputBuffer);
		
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

				const FImageTile& BufferTile = TileBufferIn.GetTile();
				const int64 TilePixelLinearIdx = BufferTile.GetIndexFromSourceCoords(ImageCoords);
				const float& TilePixelWeight = TileBufferIn.GetPixelWeight(TilePixelLinearIdx);
				float* TilePixelBuffer = TileBufferIn.GetPixel(TilePixelLinearIdx);

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

	FMeshMapBakerQueue OutputQueue(NumTiles);
	auto WriteQueuedOutput = [this, &WriteToOutputBuffer, &AddFn](FMeshMapBakerQueue& Queue)
	{
		if (Queue.AcquireProcessLock())
		{
			void* OutputData = Queue.Process();
			while (OutputData)
			{
				FMeshMapTileBuffer* TileBufferPtr = static_cast<FMeshMapTileBuffer*>(OutputData);
				WriteToOutputBuffer(*TileBufferPtr, TileBufferPtr->GetTile(), BakeAccumulateLists[static_cast<int32>(FMeshMapEvaluator::EAccumulateMode::Add)], AddFn, AddFn);
				delete TileBufferPtr;
				OutputData = Queue.Process();
			}
			Queue.ReleaseProcessLock();
		}
	};
	
	ParallelFor(NumTiles, [this, &Tiles, &GutterTexelsPerTile, &OutputQueue, &WriteToOutputBuffer, &OverwriteFn, &NoopFn, &WriteQueuedOutput](int32 TileIdx)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMapBaker::Bake_EvalTile);

		if (CancelF())
		{
			return;
		}
		
		// Generate unpadded and padded tiles.
		const FImageTile Tile = Tiles.GetTile(TileIdx);	// Image area to sample
		const FImageTile PaddedTile = Tiles.GetTile(TileIdx, TilePadding); // Filtered image area

		FImageOccupancyMap OccupancyMap;
		OccupancyMap.GutterSize = GutterSize;
		OccupancyMap.Initialize(Dimensions, PaddedTile, SamplesPerPixel);
		OccupancyMap.ComputeFromUVSpaceMesh(FlatMesh, [this](int32 TriangleID) { return FlatMesh.GetTriangleGroup(TriangleID); }, TargetMeshUVCharts);
		GutterTexelsPerTile[TileIdx] = OccupancyMap.GutterTexels;

		const int64 NumTilePixels = Tile.Num();
		for (int64 TilePixelIdx = 0; TilePixelIdx < NumTilePixels; ++TilePixelIdx)
		{
			const FVector2i SourceCoords = Tile.GetSourceCoords(TilePixelIdx);
			const int64 OccupancyMapIdx = OccupancyMap.Tile.GetIndexFromSourceCoords(SourceCoords);
			BakeAnalytics.NumSamplePixels += OccupancyMap.TexelInteriorSamples[OccupancyMapIdx];; 
		}

		FMeshMapTileBuffer* TileBuffer = new FMeshMapTileBuffer(PaddedTile, BakeSampleBufferSize);

		{
			// Evaluate valid/interior samples
			TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMapBaker::Bake_EvalTileSamples);
			
			const int TileWidth = Tile.GetWidth();
			const int TileHeight = Tile.GetHeight();
			const int32 NumSamples = OccupancyMap.PixelSampler.Num();
			for (FVector2i TileCoords(0,0); TileCoords.Y < TileHeight; ++TileCoords.Y)
			{
				for (TileCoords.X = 0; TileCoords.X < TileWidth; ++TileCoords.X)
				{
					if (CancelF())
					{
						delete TileBuffer;
						return;
					}

					const FVector2i ImageCoords = Tile.GetSourceCoords(TileCoords);
					const int64 OccupancyMapLinearIdx = OccupancyMap.Tile.GetIndexFromSourceCoords(ImageCoords);
					if (OccupancyMap.TexelNumSamples(OccupancyMapLinearIdx) == 0)
					{
						continue;
					}

					for (int32 SampleIdx = 0; SampleIdx < NumSamples; ++SampleIdx)
					{
						const int64 LinearIdx = OccupancyMapLinearIdx * NumSamples + SampleIdx;
						if (OccupancyMap.IsInterior(LinearIdx))
						{
							const FVector2d UVPosition = (FVector2d)OccupancyMap.TexelQueryUV[LinearIdx];
							const int32 UVTriangleID = OccupancyMap.TexelQueryTriangle[LinearIdx];

							FMeshMapEvaluator::FCorrespondenceSample Sample;
							DetailCorrespondenceSampler.SampleUV(UVTriangleID, UVPosition, Sample);
							if (Sample.DetailMesh && DetailSampler->IsTriangle(Sample.DetailMesh, Sample.DetailTriID))
							{
								BakeSample(*TileBuffer, Sample, UVPosition, ImageCoords, OccupancyMap);
							}
						}
					}
				}
			}
		}

		// Transfer 'Overwrite' float data to image tile buffer
		WriteToOutputBuffer(*TileBuffer, Tile, BakeAccumulateLists[static_cast<int32>(FMeshMapEvaluator::EAccumulateMode::Overwrite)], OverwriteFn, NoopFn);

		// Accumulate 'Add' float data to image tile buffer
		OutputQueue.Post(TileIdx, TileBuffer);
		WriteQueuedOutput(OutputQueue);
	}, !bParallel ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	if (CancelF())
	{
		// If cancelled, delete any outstanding tile buffers in the queue.
		while (!OutputQueue.IsDone())
		{
			void* Data = OutputQueue.Process</*bFlush*/ true>();
			if (Data)
			{
				const FMeshMapTileBuffer* TileBuffer = static_cast<FMeshMapTileBuffer*>(Data);
				delete TileBuffer;				
			}
		}
	}
	else
	{
		// The queue only acquires the process lock if the next item in the queue
		// is ready. This could mean that there are potential leftovers in the queue
		// after the parallel for. Write them out now.
		WriteQueuedOutput(OutputQueue);
	}

	if (!CancelF())
	{
		FScopedDurationTimer WriteToImageTimer(BakeAnalytics.WriteToImageDuration);
		
		// Normalize and convert ImageTileBuffer data to color data.
		ParallelFor(NumTiles, [this, &Tiles, &ImageTileBuffer](int32 TileIdx)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMapBaker::Bake_WriteToImageBuffer);
		
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
						for (const int32 Idx : BakeIds)
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
								for (int32 FloatIdx = 0; FloatIdx < static_cast<int32>(Stride); ++FloatIdx)
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
						WriteToPixel(BakeAccumulateLists[static_cast<int32>(FMeshMapEvaluator::EAccumulateMode::Add)], 1.0f / PixelWeight);
					}
					WriteToPixel(BakeAccumulateLists[static_cast<int32>(FMeshMapEvaluator::EAccumulateMode::Overwrite)], 1.0f);
				}
			}
		}, !bParallel ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
	}

	// Gutter Texel processing
	if( bGutterEnabled && !CancelF())
	{
		FScopedDurationTimer WriteToGutterTimer(BakeAnalytics.WriteToGutterDuration);
		
		const int32 NumResults = BakeResults.Num();
		ParallelFor(NumTiles, [this, &NumResults, &GutterTexelsPerTile](int32 TileIdx)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMapBaker::Bake_WriteGutterPixels);

			if (CancelF())
			{
				return;
			}

			const int NumGutter = GutterTexelsPerTile[TileIdx].Num();
			for (int64 GutterIdx = 0; GutterIdx < NumGutter; ++GutterIdx)
			{
				int64 GutterPixelTo;
				int64 GutterPixelFrom;
				Tie(GutterPixelTo, GutterPixelFrom) = GutterTexelsPerTile[TileIdx][GutterIdx];
				for (int32 Idx = 0; Idx < NumResults; Idx++)
				{
					BakeResults[Idx]->CopyPixel(GutterPixelFrom, GutterPixelTo);
				}
			}

			BakeAnalytics.NumGutterPixels += NumGutter;
		}, !bParallel ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
	}
}

void FMeshMapBaker::BakeSample(
	FMeshMapTileBuffer& TileBuffer,
	const FMeshMapEvaluator::FCorrespondenceSample& Sample,
	const FVector2d& UVPosition,
	const FVector2i& ImageCoords,
	const FImageOccupancyMap& OccupancyMap)
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

	const int64 OccupancyMapSampleIdx = OccupancyMap.Tile.GetIndexFromSourceCoords(ImageCoords);
	const int32 SampleUVChart = OccupancyMap.TexelQueryUVChart[OccupancyMapSampleIdx];

	auto AddFn = [this, &ImageCoords, &UVPosition, &Tile, &TileBuffer, Buffer, &OccupancyMap, SampleUVChart](const TArray<int32>& BakeIds) -> void
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
			const int64 OccupancyMapFilterIdx = OccupancyMap.Tile.GetIndexFromSourceCoords(SourceCoords);
			const int32 BufferTilePixelUVChart = OccupancyMap.TexelQueryUVChart[OccupancyMapFilterIdx];
			float* PixelBuffer = TileBuffer.GetPixel(BufferTilePixelLinearIdx);
			float& PixelWeight = TileBuffer.GetPixelWeight(BufferTilePixelLinearIdx);

			// Apply filter using double linear weighting (once per axis)
			FVector2d TexelDistance = Dimensions.GetTexelUV(SourceCoords) - UVPosition;
			TexelDistance.X *= Dimensions.GetWidth();
			TexelDistance.Y *= Dimensions.GetHeight();

			float FilterWeight = TextureFilterEval(TexelDistance);
			FilterWeight *= (SampleUVChart == BufferTilePixelUVChart);
			PixelWeight += FilterWeight;
			for (const int32 BakeIdx : BakeIds)
			{
				const FMeshMapEvaluator::FEvaluationContext& Context = BakeContexts[BakeIdx];
				const int32 NumData = Context.DataLayout.Num();
				const int32 ResultOffset = BakeOffsets[BakeIdx];
				for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
				{
					const int32 ResultIdx = ResultOffset + DataIdx;
					const int32 Offset = BakeSampleOffsets[ResultIdx];
					const int32 Stride = static_cast<int32>(Context.DataLayout[DataIdx]);
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
		
		for (const int32 Idx : BakeIds)
		{
			const FMeshMapEvaluator::FEvaluationContext& Context = BakeContexts[Idx];
			const int32 NumData = Context.DataLayout.Num();
			const int32 ResultOffset = BakeOffsets[Idx];
			for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
			{
				const int32 ResultIdx = ResultOffset + DataIdx;
				const int32 Offset = BakeSampleOffsets[ResultIdx];
				const int32 Stride = static_cast<int32>(Context.DataLayout[DataIdx]);
				for (int32 BufIdx = Offset; BufIdx < Offset + Stride; ++BufIdx)
				{
					PixelBuffer[BufIdx] = Buffer[BufIdx];
				}
			}
		}
	};

	AddFn(BakeAccumulateLists[static_cast<int32>(FMeshMapEvaluator::EAccumulateMode::Add)]);
	OverwriteFn(BakeAccumulateLists[static_cast<int32>(FMeshMapEvaluator::EAccumulateMode::Overwrite)]);
}

int32 FMeshMapBaker::AddEvaluator(const TSharedPtr<FMeshMapEvaluator, ESPMode::ThreadSafe>& Eval)
{
	return Bakers.Add(Eval);
}

FMeshMapEvaluator* FMeshMapBaker::GetEvaluator(const int32 EvalIdx) const
{
	return Bakers[EvalIdx].Get();
}

void FMeshMapBaker::Reset()
{
	Bakers.Empty();
	BakeResults.Empty();
}

int32 FMeshMapBaker::NumEvaluators() const
{
	return Bakers.Num();
}

const TArrayView<TUniquePtr<TImageBuilder<FVector4f>>> FMeshMapBaker::GetBakeResults(const int32 EvalIdx)
{
	const int32 ResultIdx = BakeOffsets[EvalIdx];
	const int32 NumResults = BakeOffsets[EvalIdx + 1] - ResultIdx;
	return TArrayView<TUniquePtr<TImageBuilder<FVector4f>>>(&BakeResults[ResultIdx], NumResults);
}

void FMeshMapBaker::SetDimensions(const FImageDimensions DimensionsIn)
{
	Dimensions = DimensionsIn;
}

void FMeshMapBaker::SetGutterEnabled(const bool bEnabled)
{
	bGutterEnabled = bEnabled;
}

void FMeshMapBaker::SetGutterSize(const int32 GutterSizeIn)
{
	// GutterSize must be >= 1 since it is tied to MaxDistance for the
	// OccupancyMap spatial search.
	GutterSize = GutterSizeIn >= 1 ? GutterSizeIn : 1;
}

void FMeshMapBaker::SetSamplesPerPixel(const int32 SamplesPerPixelIn)
{
	SamplesPerPixel = SamplesPerPixelIn;
}

void FMeshMapBaker::SetFilter(const EBakeFilterType FilterTypeIn)
{
	FilterType = FilterTypeIn;
}

void FMeshMapBaker::SetTileSize(const int TileSizeIn)
{
	TileSize = TileSizeIn;
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


void FMeshMapBaker::ComputeUVCharts(const FDynamicMesh3& Mesh, TArray<int32>& MeshUVCharts)
{
	MeshUVCharts.SetNumZeroed(Mesh.TriangleCount());
	if (const FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes() ? Mesh.Attributes()->PrimaryUV() : nullptr)
	{
		FMeshConnectedComponents UVComponents(&Mesh);
		UVComponents.FindConnectedTriangles();
		UVComponents.FindConnectedTriangles([UVOverlay](int32 Triangle0, int32 Triangle1) {
			return UVOverlay ? UVOverlay->AreTrianglesConnected(Triangle0, Triangle1) : false;
		});
		const int32 NumComponents = UVComponents.Num();
		for (int32 ComponentId = 0; ComponentId < NumComponents; ++ComponentId)
		{
			const FMeshConnectedComponents::FComponent& UVComp = UVComponents.GetComponent(ComponentId);
			for (const int32 TriId : UVComp.Indices)
			{
				MeshUVCharts[TriId] = ComponentId;
			}
		}
	}
}




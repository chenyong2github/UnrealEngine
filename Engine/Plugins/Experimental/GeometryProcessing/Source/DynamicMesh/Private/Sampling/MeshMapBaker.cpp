// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshMapBaker.h"
#include "Image/ImageOccupancyMap.h"
#include "Spatial/DenseGrid2.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

/**
 * Find point on Detail mesh that corresponds to point on Base mesh.
 * Strategy is:
 *    1) cast a ray inwards along -Normal from BasePoint + Thickness*Normal
 *    2) cast a ray outwards along Normal from BasePoint
 *    3) cast a ray inwards along -Normal from BasePoint
 * We take (1) preferentially, and then (2), and then (3)
 *
 * If all of those fail, if bFailToNearestPoint is true we fall back to nearest-point,
 *
 * If all the above fail, return false
 */
static bool GetDetailMeshTrianglePoint_Raycast(
	const FDynamicMesh3& DetailMesh,
	const FDynamicMeshAABBTree3& DetailSpatial,
	const FVector3d& BasePoint,
	const FVector3d& BaseNormal,
	int32& DetailTriangleOut,
	FVector3d& DetailTriBaryCoords,
	double Thickness,
	bool bFailToNearestPoint
)
{
	// TODO: should we check normals here? inverse normal should probably not be considered valid

	// shoot rays forwards and backwards
	FRay3d InwardRay = FRay3d(BasePoint + Thickness * BaseNormal, -BaseNormal);
	FRay3d ForwardRay(BasePoint, BaseNormal);
	FRay3d BackwardRay(BasePoint, -BaseNormal);
	int32 ForwardHitTID = IndexConstants::InvalidID, InwardHitTID = IndexConstants::InvalidID, BackwardHitTID = IndexConstants::InvalidID;
	double ForwardHitDist, InwardHitDist, BackwardHitDist;

	IMeshSpatial::FQueryOptions Options;
	Options.MaxDistance = Thickness;
	bool bHitInward = DetailSpatial.FindNearestHitTriangle(InwardRay, InwardHitDist, InwardHitTID, Options);
	bool bHitForward = DetailSpatial.FindNearestHitTriangle(ForwardRay, ForwardHitDist, ForwardHitTID, Options);
	bool bHitBackward = DetailSpatial.FindNearestHitTriangle(BackwardRay, BackwardHitDist, BackwardHitTID, Options);

	FRay3d HitRay;
	int32 HitTID = IndexConstants::InvalidID;
	double HitDist = TNumericLimits<double>::Max();

	if (bHitInward)
	{
		HitRay = InwardRay;
		HitTID = InwardHitTID;
		HitDist = InwardHitDist;
	}
	else if (bHitForward)
	{
		HitRay = ForwardRay;
		HitTID = ForwardHitTID;
		HitDist = ForwardHitDist;
	}
	else if (bHitBackward)
	{
		HitRay = BackwardRay;
		HitTID = BackwardHitTID;
		HitDist = BackwardHitDist;
	}

	// if we got a valid ray hit, use it
	if (DetailMesh.IsTriangle(HitTID))
	{
		DetailTriangleOut = HitTID;
		FIntrRay3Triangle3d IntrQuery = TMeshQueries<FDynamicMesh3>::TriangleIntersection(DetailMesh, HitTID, HitRay);
		DetailTriBaryCoords = IntrQuery.TriangleBaryCoords;
		return true;
	}
	else
	{
		// if we did not find any hits, try nearest-point
		IMeshSpatial::FQueryOptions OnSurfQueryOptions;
		OnSurfQueryOptions.MaxDistance = Thickness;
		double NearDistSqr = 0;
		int32 NearestTriID = -1;
		// if we are using absolute nearest point as a fallback, then ignore max distance
		if (bFailToNearestPoint)
		{
			NearestTriID = DetailSpatial.FindNearestTriangle(BasePoint, NearDistSqr);
		}
		else
		{
			NearestTriID = DetailSpatial.FindNearestTriangle(BasePoint, NearDistSqr, OnSurfQueryOptions);
		}
		if (DetailMesh.IsTriangle(NearestTriID))
		{
			DetailTriangleOut = NearestTriID;
			FDistPoint3Triangle3d DistQuery = TMeshQueries<FDynamicMesh3>::TriangleDistance(DetailMesh, NearestTriID, BasePoint);
			DetailTriBaryCoords = DistQuery.TriangleBaryCoords;
			return true;
		}
	}

	return false;
}

/**
 * Find point on Detail mesh that corresponds to point on Base mesh using minimum distance
 */
static bool GetDetailMeshTrianglePoint_Nearest(
	const FDynamicMesh3& DetailMesh,
	const FDynamicMeshAABBTree3& DetailSpatial,
	const FVector3d& BasePoint,
	int32& DetailTriangleOut,
	FVector3d& DetailTriBaryCoords)
{
	double NearDistSqr = 0;
	int32 NearestTriID = DetailSpatial.FindNearestTriangle(BasePoint, NearDistSqr);
	if (DetailMesh.IsTriangle(NearestTriID))
	{
		DetailTriangleOut = NearestTriID;
		FDistPoint3Triangle3d DistQuery = TMeshQueries<FDynamicMesh3>::TriangleDistance(DetailMesh, NearestTriID, BasePoint);
		DetailTriBaryCoords = DistQuery.TriangleBaryCoords;
		return true;
	}

	return false;
}


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
		const int32 NumData = BakeContexts[Idx].DataLayout.Num();
		for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
		{
			const int32 ResultIdx = BakeOffsets[Idx] + DataIdx;
			BakeDefaultColors[ResultIdx] = FloatToPixel(BufferPtr, BakeContexts[Idx].DataLayout[DataIdx], 1.0f);
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
	FImageTiling Tiles(Dimensions, TileSize, TileSize, 0);
	const int32 NumBakers = Bakers.Num();
	const int32 NumResults = BakeResults.Num();
	ParallelFor(Tiles.Num(), [this, &Tiles, &NumBakers, &NumResults](int32 TileIdx)
	{
		FImageDimensions Tile = Tiles.GetTile(TileIdx);

		FImageOccupancyMap OccupancyMap;
		OccupancyMap.GutterSize = GutterSize;
		OccupancyMap.Initialize(Tile, Multisampling);
		OccupancyMap.ComputeFromUVSpaceMesh(FlatMesh, [this](int32 TriangleID) { return FlatMesh.GetTriangleGroup(TriangleID); });

		FMeshMapTileBuffer TileBuffer(Tile, BakeSampleBufferSize);

		// Calculate interior texels
		for (FVector2i Texel(0,0); Texel.Y < Tile.GetHeight(); ++Texel.Y)
		{
			for (Texel.X = 0; Texel.X < Tile.GetWidth(); ++Texel.X)
			{
				if (CancelF())
				{
					return;
				}
				BakePixel(TileBuffer, OccupancyMap, Tile, Texel);
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
					const int64 ImageLinearIdx = Tile.GetSourceIndex(Texel);
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
								FVector4f& Pixel = BakeResults[ResultIdx]->GetPixel(ImageLinearIdx);
								Pixel = FloatToPixel(BufferPtr, Stride, Weight);
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

void FMeshMapBaker::BakePixel(FMeshMapTileBuffer& TileBuffer, FImageOccupancyMap& OccupancyMap, const FImageDimensions& Tile, const FVector2i& TileCoords)
{
	if (OccupancyMap.TexelNumSamples(Tile.GetIndex(TileCoords)) == 0)
	{
		return;
	}

	const int64 TilePixelLinearIdx = Tile.GetIndex(TileCoords);
	float* PixelBuffer = TileBuffer.GetPixel(TilePixelLinearIdx);
	float& PixelWeight = TileBuffer.GetPixelWeight(TilePixelLinearIdx);

	const FVector2i ImageCoords = Tile.GetSourceCoords(TileCoords.X, TileCoords.Y);
	const int32 NumSamples = OccupancyMap.Multisampler.Num();
	const int32 NumEvaluatorAdd = BakeAccumulateLists[(int32)FMeshMapEvaluator::EAccumulateMode::Add].Num();
	for (int32 SampleIdx = 0; SampleIdx < NumSamples; ++SampleIdx)
	{
		const int64 LinearIdx = TilePixelLinearIdx * OccupancyMap.Multisampler.Num() + SampleIdx;
		if (!OccupancyMap.IsInterior(LinearIdx))
		{
			PixelWeight += NumEvaluatorAdd > 0;
			for (int32 Idx : BakeAccumulateLists[(int32) FMeshMapEvaluator::EAccumulateMode::Add] )
			{
				const int32 NumData = BakeContexts[Idx].DataLayout.Num();
				for (int32 DataIdx = 0; DataIdx < NumData; ++DataIdx)
				{
					const int32 ResultIdx = BakeOffsets[Idx] + DataIdx;
					const int32 Offset = BakeSampleOffsets[ResultIdx];
					const int32 Stride = (int32) BakeContexts[Idx].DataLayout[DataIdx];
					for (int32 BufIdx = 0; BufIdx < Stride; ++BufIdx)
					{
						PixelBuffer[Offset + BufIdx] += BakeDefaults[Offset + BufIdx];
					}
				}
			}
		}
		else
		{
			const FVector2d UVPosition = (FVector2d)OccupancyMap.TexelQueryUV[LinearIdx];
			const int32 UVTriangleID = OccupancyMap.TexelQueryTriangle[LinearIdx];

			FMeshMapEvaluator::FCorrespondenceSample Sample;
			DetailMeshSampler.SampleUV(UVTriangleID, UVPosition, Sample);

			BakeSample(TileBuffer, Sample, Tile, TileCoords, ImageCoords, 1.0f);
		}
	}
}

void FMeshMapBaker::BakeSample(FMeshMapTileBuffer& TileBuffer, const FMeshMapEvaluator::FCorrespondenceSample& Sample, const FImageDimensions& Tile,
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

FVector4f FMeshMapBaker::FloatToPixel(float*& Buffer, FMeshMapEvaluator::EComponents Stride, float Weight)
{
	// TODO: Decompose this method into non-branching parts.
	FVector4f Result = FVector4f(0.0, 0.0, 0.0, 1.0f);
	switch (Stride)
	{
	case FMeshMapEvaluator::EComponents::Float1:
	{
		float X = Buffer[0] * Weight;
		Result = FVector4f(X, X, X, 1.0f);
		break;
	}
	case FMeshMapEvaluator::EComponents::Float2:
	{
		FVector2f Vec(Buffer[0], Buffer[1]);
		Vec *= Weight;
		Result = FVector4f(Vec.X, Vec.Y, 0.0f, 1.0f);
		break;
	}
	case FMeshMapEvaluator::EComponents::Float3:
	{
		FVector3f Vec(Buffer[0], Buffer[1], Buffer[2]);
		Vec *= Weight;
		Result = FVector4f(Vec.X, Vec.Y, Vec.Z, 1.0f);
		break;
	}
	case FMeshMapEvaluator::EComponents::Float4:
	{
		Result = FVector4f(Buffer[0], Buffer[1], Buffer[2], Buffer[3]);
		Result *= Weight;
		break;
	}
	default:
		break;
	}
	Buffer += static_cast<int32>(Stride);
	return Result;
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

void FMeshMapBaker::SetDetailMesh(const FDynamicMesh3* Mesh, const FDynamicMeshAABBTree3* Spatial)
{
	DetailMesh = Mesh;
	DetailSpatial = Spatial;
}

void FMeshMapBaker::SetTargetMesh(const FDynamicMesh3* Mesh)
{
	TargetMesh = Mesh;
}

void FMeshMapBaker::SetTargetMeshTangents(TSharedPtr<FMeshTangentsd, ESPMode::ThreadSafe> Tangents)
{
	TargetMeshTangents = Tangents;
}

const FDynamicMesh3* FMeshMapBaker::GetDetailMesh() const
{
	return DetailMesh;
}

const FDynamicMeshAABBTree3* FMeshMapBaker::GetDetailMeshSpatial() const
{
	return DetailSpatial;
}

const FDynamicMeshNormalOverlay* FMeshMapBaker::GetDetailMeshNormals() const
{
	check(DetailMesh && DetailMesh->HasAttributes());
	return DetailMesh->Attributes()->PrimaryNormals();
}

const FDynamicMeshUVOverlay* FMeshMapBaker::GetDetailMeshUVs(int32 UVLayerIn /*=0*/) const
{
	check(DetailMesh && DetailMesh->HasAttributes());
	return DetailMesh->Attributes()->GetUVLayer(UVLayerIn);
}

const FDynamicMesh3* FMeshMapBaker::GetTargetMesh() const
{
	return TargetMesh;
}

const FDynamicMeshUVOverlay* FMeshMapBaker::GetTargetMeshUVs() const
{
	check(TargetMesh && TargetMesh->HasAttributes() && UVLayer < TargetMesh->Attributes()->NumUVLayers());
	return TargetMesh->Attributes()->GetUVLayer(UVLayer);
}

const FDynamicMeshNormalOverlay* FMeshMapBaker::GetTargetMeshNormals() const
{
	check(TargetMesh && TargetMesh->HasAttributes());
	return TargetMesh->Attributes()->PrimaryNormals();
}

TSharedPtr<FMeshTangentsd, ESPMode::ThreadSafe> FMeshMapBaker::GetTargetMeshTangents() const
{
	return TargetMeshTangents;
}

void FMeshMapBaker::SetDimensions(FImageDimensions DimensionsIn)
{
	Dimensions = DimensionsIn;
}

void FMeshMapBaker::SetUVLayer(int32 UVLayerIn)
{
	UVLayer = UVLayerIn;
}

void FMeshMapBaker::SetThickness(double ThicknessIn)
{
	Thickness = ThicknessIn;
}

void FMeshMapBaker::SetGutterSize(int32 GutterSizeIn)
{
	GutterSize = GutterSizeIn;
}

void FMeshMapBaker::SetMultisampling(int32 MultisamplingIn)
{
	Multisampling = MultisamplingIn;
}

void FMeshMapBaker::SetCorrespondenceStrategy(ECorrespondenceStrategy Strategy)
{
	CorrespondenceStrategy = Strategy;
}



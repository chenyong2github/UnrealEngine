// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshVertexBaker.h"
#include "Sampling/MeshBakerCommon.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

FMeshConstantMapEvaluator FMeshVertexBaker::ZeroEvaluator(0.0f);
FMeshConstantMapEvaluator FMeshVertexBaker::OneEvaluator(1.0f);

void FMeshVertexBaker::Bake()
{
	if (!ensure(TargetMesh && TargetMesh->HasAttributes() && TargetMesh->Attributes()->HasPrimaryColors()))
	{
		return;
	}

	// Convert Bake mode into internal list of bakers.
	Bakers.Reset();
	if (BakeMode == EBakeMode::Color)
	{
		FMeshMapEvaluator* Evaluator = ColorEvaluator.Get();
		Bakers.Add(Evaluator ? Evaluator : &ZeroEvaluator);
		BakeInternal = &BakeImpl<EBakeMode::Color>;
	}
	else // Mode == EBakeMode::Channel
	{
		for (int Idx = 0; Idx < 4; ++Idx)
		{
			// For alpha channel, default to 1.0, otherwise 0.0.
			FMeshMapEvaluator* DefaultEvaluator = Idx == 3 ? &OneEvaluator : &ZeroEvaluator;
			FMeshMapEvaluator* Evaluator = ChannelEvaluators[Idx].Get();
			Bakers.Add(Evaluator ? Evaluator : DefaultEvaluator);
		}
		BakeInternal = &BakeImpl<EBakeMode::Channel>;
	}

	const int NumBakers = Bakers.Num();
	if (NumBakers == 0)
	{
		return;
	}

	// Initialize BakeContext(s) and BakeDefaults
	BakeDefaults = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
	float* DefaultBufferPtr = &BakeDefaults[0];
	BakeContexts.Reset();
	BakeContexts.SetNum(NumBakers);
	BakeSampleBufferSize = 0;
	for (int Idx = 0; Idx < NumBakers; ++Idx)
	{
		Bakers[Idx]->Setup(*this, BakeContexts[Idx]);

		for (FMeshMapEvaluator::EComponents Components : BakeContexts[Idx].DataLayout)
		{
			BakeSampleBufferSize += (int) Components;
		}
		if (!ensure(BakeSampleBufferSize <= 4))
		{
			return;
		}

		BakeContexts[Idx].EvaluateDefault(DefaultBufferPtr, BakeContexts[Idx].EvalData);
	}

	// Initialize BakeResult to unique vertex color elements
	const FDynamicMeshColorOverlay* ColorOverlay = TargetMesh->Attributes()->PrimaryColors();
	const int NumColors = ColorOverlay->ElementCount();
	Dimensions = FImageDimensions(NumColors, 1);

	BakeResult = MakeUnique<TImageBuilder<FVector4f>>();
	BakeResult->SetDimensions(Dimensions);
	BakeResult->Clear(BakeDefaults);

	BakeInternal(this);
}

const TImageBuilder<FVector4f>* FMeshVertexBaker::GetBakeResult() const
{
	return BakeResult.Get();
}

template<FMeshVertexBaker::EBakeMode ComputeMode>
void FMeshVertexBaker::BakeImpl(void* Data)
{
	if (!ensure(Data))
	{
		return;
	}

	FMeshVertexBaker* Baker = static_cast<FMeshVertexBaker*>(Data);
	
	ECorrespondenceStrategy UseStrategy = Baker->CorrespondenceStrategy;
	if (UseStrategy == ECorrespondenceStrategy::Identity && ensure(Baker->DetailMesh == Baker->TargetMesh) == false)
	{
		// Identity strategy requires mesh to be the same. Could potentially have two copies, in which
		// case this ensure is too conservative, but for now we will assume this
		UseStrategy = ECorrespondenceStrategy::NearestPoint;
	}
	
	const FDynamicMesh3* Mesh = Baker->TargetMesh;
	const FDynamicMeshColorOverlay* ColorOverlay = Baker->TargetMesh->Attributes()->PrimaryColors();

	// TODO: Refactor into TMeshSurfaceSampler class (future vertex bake enhancements will require non-UV based surface sampling)
	auto SampleSurface = [&Baker, &Mesh, &ColorOverlay, &UseStrategy](int32 ElementIdx,
	                                                                FMeshMapEvaluator::FCorrespondenceSample& ValueOut)
	{
		const int32 VId = ColorOverlay->GetParentVertex(ElementIdx);

		// Compute ray direction
		// TODO: Precompute a color to normal overlay mapping data structure and replace this with a lookup.
		FVector3f SurfaceNormal = FVector3f::Zero();
		TArray<int> ColorElemTris;
		ColorOverlay->GetElementTriangles(ElementIdx, ColorElemTris);
		for (int TId : ColorElemTris)
		{
			SurfaceNormal += Mesh->GetTriNormal(TId);
		}
		Normalize(SurfaceNormal);

		// Compute surface point and barycentric coords
		const FVector3d SurfacePoint = Mesh->GetVertex(VId);
		const int32 TriangleIndex = ColorElemTris[0];
		FVector3d BaryCoords = FVector3d::Zero();
		FIndex3i TriVerts = Mesh->GetTriangle(TriangleIndex);
		for (int Idx = 0; Idx < 3; ++Idx)
		{
			if (VId == TriVerts[Idx])
			{
				BaryCoords[Idx] = 1.0;
				break;
			}
		}

		ValueOut.BaseSample.TriangleIndex = TriangleIndex;
		ValueOut.BaseSample.SurfacePoint = SurfacePoint;
		ValueOut.BaseSample.BaryCoords = BaryCoords;
		ValueOut.DetailTriID = FDynamicMesh3::InvalidID;
		ValueOut.BaseNormal = SurfaceNormal;

		const FVector3d RayDir = SurfaceNormal;
		if (UseStrategy == ECorrespondenceStrategy::Identity)
		{
			ValueOut.DetailTriID = TriangleIndex;
			ValueOut.DetailBaryCoords = BaryCoords;
		}
		else if (UseStrategy == ECorrespondenceStrategy::NearestPoint)
		{
			GetDetailMeshTrianglePoint_Nearest(*Baker->DetailMesh, *Baker->DetailSpatial, SurfacePoint,
			                                   ValueOut.DetailTriID, ValueOut.DetailBaryCoords);
		}
		else // Fall back to raycast strategy
		{
			const double SampleThickness = Baker->GetThickness(); // could modulate w/ a map here...

			// Find detail mesh triangle point
			GetDetailMeshTrianglePoint_Raycast(*Baker->DetailMesh, *Baker->DetailSpatial, SurfacePoint, RayDir,
			                                   ValueOut.DetailTriID, ValueOut.DetailBaryCoords,
			                                   SampleThickness,
			                                   (UseStrategy == ECorrespondenceStrategy::RaycastStandardThenNearest));
		}
	};

	// Perform bake
	const int32 TileWidth = 256;
	const int32 TileHeight = 1;
	const FImageTiling Tiles(Baker->Dimensions, TileWidth, TileHeight);
	const int32 NumTiles = Tiles.Num();
	const int NumBakers = Baker->Bakers.Num();
	ParallelFor(NumTiles, [&Baker, &Tiles, &TileWidth, &NumBakers, &SampleSurface](const int32 TileIdx)
	{
		const FImageTile Tile = Tiles.GetTile(TileIdx);
		const int Width = Tile.GetWidth();
		for (int32 Idx = 0; Idx < Width; ++Idx)
		{
			const int ElemIdx = TileIdx * TileWidth + Idx;
			FMeshMapEvaluator::FCorrespondenceSample Sample;
			SampleSurface(ElemIdx, Sample);

			FVector4f& Pixel = Baker->BakeResult->GetPixel(ElemIdx);
			float* BufferPtr = &Pixel[0];
			for (int32 BakerIdx = 0; BakerIdx < NumBakers; ++BakerIdx)
			{
				Baker->BakeContexts[BakerIdx].Evaluate(BufferPtr, Sample, Baker->BakeContexts[BakerIdx].EvalData);
			}

			// For color bakes, ask our evaluators to convert the float data to color.
			if constexpr(ComputeMode == EBakeMode::Color)
			{
				// TODO: Use a separate buffer rather than R/W from the same pixel.
				BufferPtr = &Pixel[0];
				for (int32 BakerIdx = 0; BakerIdx < NumBakers; ++BakerIdx)
				{
					Baker->BakeContexts[BakerIdx].EvaluateColor(0, BufferPtr, Pixel, Baker->BakeContexts[BakerIdx].EvalData);
				}
			}
		}
	}, !Baker->bParallel ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
}

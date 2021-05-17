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

void FMeshMapBaker::Bake()
{
	if (Bakers.IsEmpty())
	{
		return;
	}

	// Initialize Bake targets and results.
	BakeResults.SetNum(Bakers.Num());
	for (int32 Idx = 0; Idx < Bakers.Num(); Idx++)
	{
		BakeResults[Idx] = MakeShared<TImageBuilder<FVector4f>, ESPMode::ThreadSafe>();
		BakeResults[Idx]->SetDimensions(Dimensions);
		FVector4f ClearColor = Bakers[Idx]->DefaultSample();
		BakeResults[Idx]->Clear(ClearColor);
		Bakers[Idx]->PreEvaluate(*this);
	}

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

	// this sampler finds the correspondence between base surface and detail surface
	DetailMeshSampler.Initialize(Mesh, UVOverlay, EMeshSurfaceSamplerQueryType::TriangleAndUV, FMeshImageBaker::FCorrespondenceSample(),
		[Mesh, NormalOverlay, UseStrategy, this](const FMeshUVSampleInfo& SampleInfo, FMeshImageBaker::FCorrespondenceSample& ValueOut)
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
		else	// fall back to raycast strategy
		{
			double SampleThickness = this->GetThickness();		// could modulate w/ a map here...

			// find detail mesh triangle point
			bool bFoundTri = GetDetailMeshTrianglePoint_Raycast(*DetailMesh, *DetailSpatial, SampleInfo.SurfacePoint, RayDir,
				ValueOut.DetailTriID, ValueOut.DetailBaryCoords, SampleThickness,
				(UseStrategy == ECorrespondenceStrategy::RaycastStandardThenNearest));
		}

	});

	// Setup image tiling
	FImageTiling Tiles(Dimensions, TileSize, TileSize, 0);
	ParallelFor(Tiles.Num(), [this, &Tiles](int32 TileIdx)
	{
		FImageDimensions Tile = Tiles.GetTile(TileIdx);

		FImageOccupancyMap OccupancyMap;
		OccupancyMap.GutterSize = GutterSize;
		OccupancyMap.Initialize(Tile, Multisampling);
		OccupancyMap.ComputeFromUVSpaceMesh(FlatMesh, [this](int32 TriangleID) { return FlatMesh.GetTriangleGroup(TriangleID); });

		// calculate interior texels
		for (int32 ImgY = 0; ImgY < Tile.GetHeight(); ImgY++)
		{
			for (int32 ImgX = 0; ImgX < Tile.GetWidth(); ImgX++)
			{
				if (CancelF())
				{
					return;
				}

				BakePixel(OccupancyMap, Tile, ImgX, ImgY);
			}
		}

		for (int64 k = 0; k < OccupancyMap.GutterTexels.Num(); k++)
		{
			TPair<int64, int64> GutterTexel = OccupancyMap.GutterTexels[k];
			for (int32 Idx = 0; Idx < Bakers.Num(); Idx++)
			{
				BakeResults[Idx]->CopyPixel(GutterTexel.Value, GutterTexel.Key);
			}
		}
	}, !bParallel ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	for (int32 Idx = 0; Idx < Bakers.Num(); Idx++)
	{
		Bakers[Idx]->PostEvaluate(*this, *BakeResults[Idx]);
	}
}

void FMeshMapBaker::BakePixel(FImageOccupancyMap& OccupancyMap, const FImageDimensions& Tile, int32 ImgX, int32 ImgY)
{
	FVector2i ImageCoords = Tile.GetSourceCoords(ImgX, ImgY);

	// If this texel contains any interior samples, we will accumulate
	// the result, reset the clear color to black.
	if (OccupancyMap.TexelNumSamples(Tile.GetIndex(ImgX, ImgY)) > 0)
	{
		for (int32 Idx = 0; Idx < Bakers.Num(); Idx++)
		{
			if (Bakers[Idx]->SupportsMultisampling())
			{
				BakeResults[Idx]->SetPixel(ImageCoords, FVector4f::Zero());
			}
		}
	}
	else
	{
		return;
	}

	for (int32 SampleIdx = 0; SampleIdx < OccupancyMap.Multisampler.Num(); SampleIdx++)
	{
		int64 LinearIdx = Tile.GetIndex(ImgX, ImgY) * OccupancyMap.Multisampler.Num() + SampleIdx;
		double OneOverNumSamples = 1.0 / (double)OccupancyMap.Multisampler.Num();
		if (OccupancyMap.IsInterior(LinearIdx) == false)
		{
			for (int32 Idx = 0; Idx < Bakers.Num(); Idx++)
			{
				if (Bakers[Idx]->SupportsMultisampling())
				{
					FVector4f Result = BakeResults[Idx]->GetPixel(ImageCoords);
					Result += Bakers[Idx]->DefaultSample() * OneOverNumSamples;
					BakeResults[Idx]->SetPixel(ImageCoords, Result);
				}
			}
		}
		else
		{
			FVector2d UVPosition = (FVector2d)OccupancyMap.TexelQueryUV[LinearIdx];
			int32 UVTriangleID = OccupancyMap.TexelQueryTriangle[LinearIdx];

			FMeshImageBaker::FCorrespondenceSample Sample;
			DetailMeshSampler.SampleUV(UVTriangleID, UVPosition, Sample);

			for (int32 Idx = 0; Idx < Bakers.Num(); Idx++)
			{
				FVector4f SampleResult = Bakers[Idx]->EvaluateSample(*this, Sample);
				if (!Bakers[Idx]->SupportsMultisampling())
				{
					// TODO/FIXME: Can be more correct & optimal here by not multisampling at all.
					// Currently the samples are precomputed in the occupancy map.
					BakeResults[Idx]->SetPixel(ImageCoords, SampleResult);
				}
				else
				{
					FVector4f Result = BakeResults[Idx]->GetPixel(ImageCoords);
					Result += SampleResult * OneOverNumSamples;
					BakeResults[Idx]->SetPixel(ImageCoords, Result);
				}
			}
		}
	}
}

int32 FMeshMapBaker::AddBaker(TSharedPtr<FMeshImageBaker> Sampler)
{
	return Bakers.Add(Sampler);
}

void FMeshMapBaker::Reset()
{
	Bakers.Empty();
	BakeResults.Empty();
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



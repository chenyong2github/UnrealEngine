// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshOcclusionMapBaker.h"
#include "Sampling/SphericalFibonacci.h"
#include "Sampling/Gaussians.h"

#include "Math/RandomStream.h"


void FMeshOcclusionMapBaker::Bake()
{
	const FMeshImageBakingCache* BakeCache = GetCache();
	check(BakeCache);
	const FDynamicMesh3* DetailMesh = BakeCache->GetDetailMesh();
	const FDynamicMeshAABBTree3* DetailSpatial = BakeCache->GetDetailSpatial();
	const FDynamicMeshNormalOverlay* DetailNormalOverlay = BakeCache->GetDetailNormals();
	check(DetailNormalOverlay);

	double BiasDotThreshold = FMathd::Cos(FMathd::Clamp(90.0 - BiasAngleDeg, 0.0, 90.0) * FMathd::DegToRad);

	// precompute ray directions for AO
	TSphericalFibonacci<double> Points(2 * NumOcclusionRays);
	TArray<FVector3d> RayDirections;
	for (int32 k = 0; k < Points.Num(); ++k)
	{
		FVector3d P = Points[k];
		if (P.Z > 0)
		{
			RayDirections.Add(P.Normalized());
		}
	}

	// 
	FRandomStream RotationGen(31337);
	FCriticalSection RotationLock;
	auto GetRandomRotation = [&RotationGen, &RotationLock]() {
		RotationLock.Lock();
		double Angle = RotationGen.GetFraction() * FMathd::TwoPi;
		RotationLock.Unlock();
		return Angle;
	};


	auto OcclusionSampleFunction = [&](const FMeshImageBakingCache::FCorrespondenceSample& SampleData)
	{
		int32 DetailTriID = SampleData.DetailTriID;
		if (DetailMesh->IsTriangle(DetailTriID))
		{
			FIndex3i DetailTri = DetailMesh->GetTriangle(DetailTriID);
			//FVector3d DetailTriNormal = DetailMesh.GetTriNormal(DetailTriID);
			FVector3d DetailTriNormal;
			DetailNormalOverlay->GetTriBaryInterpolate<double>(DetailTriID, &SampleData.DetailBaryCoords[0], &DetailTriNormal.X);
			DetailTriNormal.Normalize();

			FVector3d DetailBaryCoords = SampleData.DetailBaryCoords;
			FVector3d DetailPos = DetailMesh->GetTriBaryPoint(DetailTriID, DetailBaryCoords.X, DetailBaryCoords.Y, DetailBaryCoords.Z);
			DetailPos += 10.0f * FMathf::ZeroTolerance * DetailTriNormal;
			FFrame3d SurfaceFrame(DetailPos, DetailTriNormal);

			double RotationAngle = GetRandomRotation();
			SurfaceFrame.Rotate(FQuaterniond(SurfaceFrame.Z(), RotationAngle, false));

			IMeshSpatial::FQueryOptions QueryOptions;
			QueryOptions.MaxDistance = MaxDistance;

			double AccumOcclusion = 0;
			double TotalWeight = 0;
			for (FVector3d SphereDir : RayDirections)
			{
				FRay3d OcclusionRay(DetailPos, SurfaceFrame.FromFrameVector(SphereDir));
				check(OcclusionRay.Direction.Dot(DetailTriNormal) > 0);

				// Have weight of point fall off as it becomes more coplanar with face. 
				// This reduces faceting artifacts that we would otherwise see because geometry does not vary smoothly
				double PointWeight = 1.0;
				double BiasDot = OcclusionRay.Direction.Dot(DetailTriNormal);
				if (BiasDot < BiasDotThreshold)
				{
					PointWeight = FMathd::Lerp(0.0, 1.0, FMathd::Clamp(BiasDot / BiasDotThreshold, 0.0, 1.0));
					PointWeight *= PointWeight;
				}
				TotalWeight += PointWeight;

				if (DetailSpatial->TestAnyHitTriangle(OcclusionRay, QueryOptions))
				{
					AccumOcclusion += PointWeight;
				}
			}

			AccumOcclusion = (TotalWeight > 0.0001) ? (AccumOcclusion / TotalWeight) : 0.0;
			return AccumOcclusion;
		}
		return 0.0;
	};

	OcclusionBuilder = MakeUnique<TImageBuilder<FVector3f>>();
	OcclusionBuilder->SetDimensions(BakeCache->GetDimensions());

	BakeCache->EvaluateSamples([&](const FVector2i& Coords, const FMeshImageBakingCache::FCorrespondenceSample& Sample)
	{
		double Occlusion = OcclusionSampleFunction(Sample);
		FVector3f OcclusionColor = FMathd::Clamp(1.0f - (float)Occlusion, 0.0f, 1.0f) * FVector3f::One();
		OcclusionBuilder->SetPixel(Coords, OcclusionColor);
	});

	const FImageOccupancyMap& Occupancy = *BakeCache->GetOccupancyMap();

	for (int64 k = 0; k < Occupancy.GutterTexels.Num(); k++)
	{
		TPair<int64, int64> GutterTexel = Occupancy.GutterTexels[k];
		OcclusionBuilder->CopyPixel(GutterTexel.Value, GutterTexel.Key);
	}

	if (BlurRadius > 0.01)
	{
		TDiscreteKernel2f BlurKernel2d;
		TGaussian2f::MakeKernelFromRadius(BlurRadius, BlurKernel2d);
		TArray<float> AOBlurBuffer;
		Occupancy.ParallelProcessingPass<float>(
			[&](int64 Index) { return 0.0f; },
			[&](int64 LinearIdx, float Weight, float& CurValue) { CurValue += Weight * OcclusionBuilder->GetPixel(LinearIdx).X; },
			[&](int64 LinearIdx, float WeightSum, float& CurValue) { CurValue /= WeightSum; },
			[&](int64 LinearIdx, float& CurValue) { OcclusionBuilder->SetPixel(LinearIdx, FVector3f(CurValue, CurValue, CurValue)); },
			[&](const FVector2i& TexelOffset) { return BlurKernel2d.EvaluateFromOffset(TexelOffset); },
			BlurKernel2d.IntRadius,
			AOBlurBuffer);
	}



}
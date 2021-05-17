// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshOcclusionMapBaker.h"
#include "Sampling/MeshMapBaker.h"
#include "Sampling/SphericalFibonacci.h"
#include "Sampling/Gaussians.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

void FMeshOcclusionMapBaker::Bake()
{
	if (OcclusionType == EOcclusionMapType::None)
	{
		return;
	}

	const FMeshImageBakingCache* BakeCache = GetCache();
	check(BakeCache);
	DetailMesh = BakeCache->GetDetailMesh();
	DetailSpatial = BakeCache->GetDetailSpatial();
	DetailNormalOverlay = BakeCache->GetDetailNormals();
	check(DetailNormalOverlay);

	BiasDotThreshold = FMathd::Cos(FMathd::Clamp(90.0 - BiasAngleDeg, 0.0, 90.0) * FMathd::DegToRad);

	// Precompute occlusion ray directions
	RayDirections.Empty(NumOcclusionRays);
	THemisphericalFibonacci<double>::EDistribution Dist = THemisphericalFibonacci<double>::EDistribution::Cosine;
	switch (Distribution)
	{
	case EDistribution::Uniform:
		Dist = THemisphericalFibonacci<double>::EDistribution::Uniform;
		break;
	case EDistribution::Cosine:
		Dist = THemisphericalFibonacci<double>::EDistribution::Cosine;
		break;
	}
	THemisphericalFibonacci<double> Points(NumOcclusionRays, Dist);
	for (int32 k = 0; k < Points.Num(); ++k)
	{
		FVector3d P = Points[k];
		if (P.Z > 0)
		{
			RayDirections.Add( Normalized(P) );
		}
	}

	// Map occlusion ray hemisphere to conical area (SpreadAngle/2)
	double ConicalAngle = FMathd::Clamp(SpreadAngle * 0.5, 0.0001, 90.0);
	for (int32 k = 0; k < RayDirections.Num(); ++k)
	{
		FVector3d& RayDir = RayDirections[k];
		double RayAngle = AngleD(RayDir,FVector3d::UnitZ());
		FVector3d RayCross = RayDir.Cross(FVector3d::UnitZ());
		double RotationAngle = RayAngle - FMathd::Lerp(0.0, ConicalAngle, RayAngle / 90.0);
		FQuaterniond Rotation(RayCross, RotationAngle, true);
		RayDir = Rotation * RayDir;
	}

	if (WantAmbientOcclusion())
	{
		OcclusionBuilder = MakeUnique<TImageBuilder<FVector3f>>();
		OcclusionBuilder->SetDimensions(BakeCache->GetDimensions());
		OcclusionBuilder->Clear(FVector3f::One());
	}
	if (WantBentNormal())
	{
		NormalBuilder = MakeUnique<TImageBuilder<FVector3f>>();
		NormalBuilder->SetDimensions(BakeCache->GetDimensions());
		FVector3f DefaultNormal = FVector3f::UnitZ();
		switch (NormalSpace)
		{
		case ESpace::Tangent:
			break;
		case ESpace::Object:
			DefaultNormal = FVector3f::Zero();
			break;
		}
		FVector3f DefaultMapNormal = (DefaultNormal + FVector3f::One()) * 0.5f;
		NormalBuilder->Clear(DefaultMapNormal);
	}

	BakeCache->EvaluateSamples([&](const FVector2i& Coords, const FMeshImageBakingCache::FCorrespondenceSample& Sample)
	{
		double Occlusion = 0.0;
		FVector3d BentNormal = FVector3f::UnitZ();
		SampleFunction<FMeshImageBakingCache::FCorrespondenceSample>(Sample, Occlusion, BentNormal);
		if (WantAmbientOcclusion())
		{
			FVector3f OcclusionColor = FMathd::Clamp(1.0f - (float)Occlusion, 0.0f, 1.0f) * FVector3f::One();
			OcclusionBuilder->SetPixel(Coords, OcclusionColor);
		}
		if (WantBentNormal())
		{
			FVector3f NormalColor = (FVector3f(BentNormal.X, BentNormal.Y, BentNormal.Z) + FVector3f::One()) * 0.5;
			NormalBuilder->SetPixel(Coords, NormalColor);
		}
	});

	const FImageOccupancyMap& Occupancy = *BakeCache->GetOccupancyMap();
	for (int64 k = 0; k < Occupancy.GutterTexels.Num(); k++)
	{
		TPair<int64, int64> GutterTexel = Occupancy.GutterTexels[k];
		if (WantAmbientOcclusion())
		{
			OcclusionBuilder->CopyPixel(GutterTexel.Value, GutterTexel.Key);
		}
		if (WantBentNormal())
		{
			NormalBuilder->CopyPixel(GutterTexel.Value, GutterTexel.Key);
		}
	}

	if (WantAmbientOcclusion() && BlurRadius > 0.01)
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

void FMeshOcclusionMapBaker::PreEvaluate(const FMeshMapBaker& Baker)
{
	// TODO: Support evaluation all map types in single pass.
	check(OcclusionType != EOcclusionMapType::None && OcclusionType != EOcclusionMapType::All);

	DetailMesh = Baker.GetDetailMesh();
	DetailSpatial = Baker.GetDetailMeshSpatial();
	DetailNormalOverlay = Baker.GetDetailMeshNormals();
	check(DetailNormalOverlay);

	if (WantBentNormal() && NormalSpace == ESpace::Tangent)
	{
		BaseMeshTangents = Baker.GetTargetMeshTangents().Get();
	}

	BiasDotThreshold = FMathd::Cos(FMathd::Clamp(90.0 - BiasAngleDeg, 0.0, 90.0) * FMathd::DegToRad);

	// Precompute occlusion ray directions
	RayDirections.Empty(NumOcclusionRays);
	THemisphericalFibonacci<double>::EDistribution Dist = THemisphericalFibonacci<double>::EDistribution::Cosine;
	switch (Distribution)
	{
	case EDistribution::Uniform:
		Dist = THemisphericalFibonacci<double>::EDistribution::Uniform;
		break;
	case EDistribution::Cosine:
		Dist = THemisphericalFibonacci<double>::EDistribution::Cosine;
		break;
	}
	THemisphericalFibonacci<double> Points(NumOcclusionRays, Dist);
	for (int32 k = 0; k < Points.Num(); ++k)
	{
		FVector3d P = Points[k];
		if (P.Z > 0)
		{
			RayDirections.Add(Normalized(P));
		}
	}

	// Map occlusion ray hemisphere to conical area (SpreadAngle/2)
	double ConicalAngle = FMathd::Clamp(SpreadAngle * 0.5, 0.0001, 90.0);
	for (int32 k = 0; k < RayDirections.Num(); ++k)
	{
		FVector3d& RayDir = RayDirections[k];
		double RayAngle = AngleD(RayDir, FVector3d::UnitZ());
		FVector3d RayCross = RayDir.Cross(FVector3d::UnitZ());
		double RotationAngle = RayAngle - FMathd::Lerp(0.0, ConicalAngle, RayAngle / 90.0);
		FQuaterniond Rotation(RayCross, RotationAngle, true);
		RayDir = Rotation * RayDir;
	}
}

FVector4f FMeshOcclusionMapBaker::EvaluateSample(const FMeshMapBaker& Baker, const FCorrespondenceSample& Sample)
{
	double Occlusion = 0.0;
	FVector3d BentNormal = FVector3f::UnitZ();
	SampleFunction<FCorrespondenceSample>(Sample, Occlusion, BentNormal);
	if (WantAmbientOcclusion())
	{
		FVector3f OcclusionColor = FMathd::Clamp(1.0f - (float)Occlusion, 0.0f, 1.0f) * FVector3f::One();
		return FVector4f(OcclusionColor.X, OcclusionColor.Y, OcclusionColor.Z, 1.0f);
	}
	else //if (WantBentNormal())
	{
		// Map normal space [-1,1] to floating point color space [0,1]
		FVector3f NormalColor = (FVector3f(BentNormal.X, BentNormal.Y, BentNormal.Z) + FVector3f::One()) * 0.5;
		return FVector4f(NormalColor.X, NormalColor.Y, NormalColor.Z, 1.0f);
	}
}

template<class SampleType>
void FMeshOcclusionMapBaker::SampleFunction(const SampleType& SampleData, double& OutOcclusion, FVector3d& OutNormal)
{
	// Fallback normal if invalid Tri or fully occluded
	FVector3d DefaultNormal = FVector3d::UnitZ();
	switch (NormalSpace)
	{
	case ESpace::Tangent:
		break;
	case ESpace::Object:
		DefaultNormal = SampleData.BaseNormal;
		break;
	}

	int32 DetailTriID = SampleData.DetailTriID;
	if (!DetailMesh->IsTriangle(DetailTriID))
	{
		OutOcclusion = 0.0;
		OutNormal = DefaultNormal;
		return;
	}

	FIndex3i DetailTri = DetailMesh->GetTriangle(DetailTriID);
	//FVector3d DetailTriNormal = DetailMesh.GetTriNormal(DetailTriID);
	FVector3d DetailTriNormal;
	DetailNormalOverlay->GetTriBaryInterpolate<double>(DetailTriID, &SampleData.DetailBaryCoords.X, &DetailTriNormal.X);
	Normalize(DetailTriNormal);

	FVector3d BaseTangentX, BaseTangentY;
	if (WantBentNormal() && NormalSpace == ESpace::Tangent)
	{
		check(BaseMeshTangents);
		BaseMeshTangents->GetInterpolatedTriangleTangent(
			SampleData.BaseSample.TriangleIndex,
			SampleData.BaseSample.BaryCoords,
			BaseTangentX, BaseTangentY);
	}

	FVector3d DetailBaryCoords = SampleData.DetailBaryCoords;
	FVector3d DetailPos = DetailMesh->GetTriBaryPoint(DetailTriID, DetailBaryCoords.X, DetailBaryCoords.Y, DetailBaryCoords.Z);
	DetailPos += 10.0f * FMathf::ZeroTolerance * DetailTriNormal;
	FFrame3d SurfaceFrame(DetailPos, DetailTriNormal);

	double RotationAngle = GetRandomRotation();
	SurfaceFrame.Rotate(FQuaterniond(SurfaceFrame.Z(), RotationAngle, false));

	IMeshSpatial::FQueryOptions QueryOptions;
	QueryOptions.MaxDistance = MaxDistance;

	double AccumOcclusion = 0;
	FVector3d AccumNormal(FVector3d::Zero());
	double TotalPointWeight = 0;
	for (FVector3d SphereDir : RayDirections)
	{
		FRay3d OcclusionRay(DetailPos, SurfaceFrame.FromFrameVector(SphereDir));
		ensure(OcclusionRay.Direction.Dot(DetailTriNormal) > 0);

		bool bHit = DetailSpatial->TestAnyHitTriangle(OcclusionRay, QueryOptions);

		if (WantAmbientOcclusion())
		{
			// Have weight of point fall off as it becomes more coplanar with face. 
			// This reduces faceting artifacts that we would otherwise see because geometry does not vary smoothly
			double PointWeight = 1.0;
			double BiasDot = OcclusionRay.Direction.Dot(DetailTriNormal);
			if (BiasDot < BiasDotThreshold)
			{
				PointWeight = FMathd::Lerp(0.0, 1.0, FMathd::Clamp(BiasDot / BiasDotThreshold, 0.0, 1.0));
				PointWeight *= PointWeight;
			}
			TotalPointWeight += PointWeight;

			if (bHit)
			{
				AccumOcclusion += PointWeight;
			}
		}

		if (WantBentNormal() && !bHit)
		{
			FVector3d BentNormal = OcclusionRay.Direction;
			if (NormalSpace == ESpace::Tangent)
			{
				// compute normal in tangent space
				double dx = BentNormal.Dot(BaseTangentX);
				double dy = BentNormal.Dot(BaseTangentY);
				double dz = BentNormal.Dot(SampleData.BaseNormal);
				BentNormal = FVector3d(dx, dy, dz);;
			}
			AccumNormal += BentNormal;
		}
	}

	if (WantAmbientOcclusion())
	{
		AccumOcclusion = (TotalPointWeight > 0.0001) ? (AccumOcclusion / TotalPointWeight) : 0.0;
	}
	if (WantBentNormal())
	{
		AccumNormal = (AccumNormal.Length() > 0.0) ? Normalized(AccumNormal) : DefaultNormal;
	}
	OutOcclusion = AccumOcclusion;
	OutNormal = AccumNormal;
}

double FMeshOcclusionMapBaker::GetRandomRotation()
{
	// Randomized rotation of occlusion rays about the normal
	RotationLock.Lock();
	double Angle = RotationGen.GetFraction() * FMathd::TwoPi;
	RotationLock.Unlock();
	return Angle;
}


// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshCurvatureMapBaker.h"
#include "Sampling/MeshMapBaker.h"
#include "Sampling/SphericalFibonacci.h"
#include "Sampling/Gaussians.h"
#include "MeshCurvature.h"
#include "MeshWeights.h"
#include "MeshNormals.h"

#include "Math/RandomStream.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

void FMeshCurvatureMapBaker::CacheDetailCurvatures(const FDynamicMesh3* DetailMeshIn)
{
	if (! Curvatures)
	{
		Curvatures = MakeShared<FMeshVertexCurvatureCache>();
		Curvatures->BuildAll(*DetailMeshIn);
	}
	check(Curvatures->Num() == DetailMeshIn->MaxVertexID());

}


void FMeshCurvatureMapBaker::Bake()
{
	const FMeshImageBakingCache* BakeCache = GetCache();
	check(BakeCache);
	DetailMesh = BakeCache->GetDetailMesh();

	CacheDetailCurvatures(DetailMesh);

	ResultBuilder = MakeUnique<TImageBuilder<FVector3f>>();
	ResultBuilder->SetDimensions(BakeCache->GetDimensions());
	ResultBuilder->Clear(FVector3f::Zero());


	Bake_Single();


	const FImageOccupancyMap& Occupancy = *BakeCache->GetOccupancyMap();

	for (int64 k = 0; k < Occupancy.GutterTexels.Num(); k++)
	{
		TPair<int64, int64> GutterTexel = Occupancy.GutterTexels[k];
		ResultBuilder->CopyPixel(GutterTexel.Value, GutterTexel.Key);
	}

	if (BlurRadius > 0.01)
	{
		TDiscreteKernel2f BlurKernel2d;
		TGaussian2f::MakeKernelFromRadius(BlurRadius, BlurKernel2d);
		TArray<FVector3f> AOBlurBuffer;
		Occupancy.ParallelProcessingPass<FVector3f>(
			[&](int64 Index) { return FVector3f::Zero(); },
			[&](int64 LinearIdx, float Weight, FVector3f& CurValue) { CurValue += Weight * ResultBuilder->GetPixel(LinearIdx); },
			[&](int64 LinearIdx, float WeightSum, FVector3f& CurValue) { CurValue /= WeightSum; },
			[&](int64 LinearIdx, FVector3f& CurValue) { ResultBuilder->SetPixel(LinearIdx, CurValue); },
			[&](const FVector2i& TexelOffset) { return BlurKernel2d.EvaluateFromOffset(TexelOffset); },
			BlurKernel2d.IntRadius,
			AOBlurBuffer);
	}

}


void FMeshCurvatureMapBaker::Bake_Single()
{
	const FMeshImageBakingCache* BakeCache = GetCache();
	const FMeshVertexCurvatureCache& Cache = *Curvatures;

	MinPreClamp = -TNumericLimits<double>::Max();
	MaxPreClamp = TNumericLimits<double>::Max();
	if (UseClampMode == EClampMode::Positive)
	{
		MinPreClamp = 0;
	}
	else if (UseClampMode == EClampMode::Negative)
	{
		MaxPreClamp = 0;
	}

	FSampleSetStatisticsd CurvatureStats = Cache.MeanStats;
	switch (UseCurvatureType)
	{
	default:
	case ECurvatureType::Mean:
		CurvatureStats = Cache.MeanStats;
		break;
	case ECurvatureType::Gaussian:
		CurvatureStats = Cache.GaussianStats;
		break;
	case ECurvatureType::MaxPrincipal:
		CurvatureStats = Cache.MaxPrincipalStats;
		break;
	case ECurvatureType::MinPrincipal:
		CurvatureStats = Cache.MinPrincipalStats;
		break;
	}

	double ClampMax = RangeScale * (CurvatureStats.Mean + CurvatureStats.StandardDeviation);
	if (bOverrideCurvatureRange)
	{
		ClampMax = RangeScale * OverrideRangeMax;
	}
	double MinClamp = MinRangeScale * ClampMax;
	ClampRange = FInterval1d(MinClamp, ClampMax);

	GetColorMapRange(NegativeColor, ZeroColor, PositiveColor);

	BakeCache->EvaluateSamples([this](const FVector2i& Coords, const FMeshImageBakingCache::FCorrespondenceSample& Sample)
	{
		double Curvature = SampleFunction<FMeshImageBakingCache::FCorrespondenceSample>(Sample);

		double Sign = FMathd::Sign(Curvature);
		float T = (float)ClampRange.GetT(FMathd::Abs(Curvature));

		FVector3f CurvatureColor = (Sign < 0) ?
			Lerp(ZeroColor, NegativeColor, T) : Lerp(ZeroColor, PositiveColor, T);

		ResultBuilder->SetPixel(Coords, CurvatureColor);
	});
}


void FMeshCurvatureMapBaker::Bake_Multi()
{

}

double FMeshCurvatureMapBaker::GetCurvature(int32 vid)
{
	const FMeshVertexCurvatureCache& Cache = *Curvatures;
	switch (UseCurvatureType)
	{
	default:
	case ECurvatureType::Mean:
		return FMathd::Clamp(Cache[vid].Mean, MinPreClamp, MaxPreClamp);
	case ECurvatureType::Gaussian:
		return FMathd::Clamp(Cache[vid].Gaussian, MinPreClamp, MaxPreClamp);
	case ECurvatureType::MaxPrincipal:
		return FMathd::Clamp(Cache[vid].MaxPrincipal, MinPreClamp, MaxPreClamp);
	case ECurvatureType::MinPrincipal:
		return FMathd::Clamp(Cache[vid].MinPrincipal, MinPreClamp, MaxPreClamp);
	}
}

void FMeshCurvatureMapBaker::GetColorMapRange(FVector3f& NegativeColorOut, FVector3f& ZeroColorOut, FVector3f& PositiveColorOut)
{
	switch (UseColorMode)
	{
	default:
	case EColorMode::BlackGrayWhite:
		NegativeColorOut = FVector3f(0, 0, 0);
		ZeroColorOut = FVector3f(0.5, 0.5, 0.5);
		PositiveColorOut = FVector3f(1, 1, 1);
		break;
	case EColorMode::RedGreenBlue:
		NegativeColorOut = FVector3f(1, 0, 0);
		ZeroColorOut = FVector3f(0, 1, 0);
		PositiveColorOut = FVector3f(0, 0, 1);
		break;
	case EColorMode::RedBlue:
		NegativeColorOut = FVector3f(1, 0, 0);
		ZeroColorOut = FVector3f(0, 0, 0);
		PositiveColorOut = FVector3f(0, 0, 1);
		break;
	}
}

void FMeshCurvatureMapBaker::PreEvaluate(const FMeshMapBaker& Baker)
{
	DetailMesh = Baker.GetDetailMesh();
	CacheDetailCurvatures(DetailMesh);

	MinPreClamp = -TNumericLimits<double>::Max();
	MaxPreClamp = TNumericLimits<double>::Max();
	if (UseClampMode == EClampMode::Positive)
	{
		MinPreClamp = 0;
	}
	else if (UseClampMode == EClampMode::Negative)
	{
		MaxPreClamp = 0;
	}

	const FMeshVertexCurvatureCache& Cache = *Curvatures;
	FSampleSetStatisticsd CurvatureStats = Cache.MeanStats;
	switch (UseCurvatureType)
	{
	default:
	case ECurvatureType::Mean:
		CurvatureStats = Cache.MeanStats;
		break;
	case ECurvatureType::Gaussian:
		CurvatureStats = Cache.GaussianStats;
		break;
	case ECurvatureType::MaxPrincipal:
		CurvatureStats = Cache.MaxPrincipalStats;
		break;
	case ECurvatureType::MinPrincipal:
		CurvatureStats = Cache.MinPrincipalStats;
		break;
	}

	double ClampMax = RangeScale * (CurvatureStats.Mean + CurvatureStats.StandardDeviation);
	if (bOverrideCurvatureRange)
	{
		ClampMax = RangeScale * OverrideRangeMax;
	}
	double MinClamp = MinRangeScale * ClampMax;
	ClampRange = FInterval1d(MinClamp, ClampMax);

	GetColorMapRange(NegativeColor, ZeroColor, PositiveColor);
}

FVector4f FMeshCurvatureMapBaker::EvaluateSample(const FMeshMapBaker& Baker, const FCorrespondenceSample& Sample)
{
	double Curvature = SampleFunction<FCorrespondenceSample>(Sample);
	double Sign = FMathd::Sign(Curvature);
	float T = (float)ClampRange.GetT(FMathd::Abs(Curvature));
	FVector3f CurvatureColor = (Sign < 0) ?
		Lerp(ZeroColor, NegativeColor, T) : Lerp(ZeroColor, PositiveColor, T);
	return FVector4f(CurvatureColor.X, CurvatureColor.Y, CurvatureColor.Z, 1.0f);
}

template<class SampleType>
double FMeshCurvatureMapBaker::SampleFunction(const SampleType& SampleData)
{
	int32 DetailTriID = SampleData.DetailTriID;
	if (DetailMesh->IsTriangle(DetailTriID))
	{
		FIndex3i DetailTri = DetailMesh->GetTriangle(DetailTriID);
		double CurvatureA = GetCurvature(DetailTri.A);
		double CurvatureB = GetCurvature(DetailTri.B);
		double CurvatureC = GetCurvature(DetailTri.C);
		double InterpCurvature = SampleData.DetailBaryCoords.X * CurvatureA
			+ SampleData.DetailBaryCoords.Y * CurvatureB
			+ SampleData.DetailBaryCoords.Z * CurvatureC;

		return InterpCurvature;
	}
	return 0.0;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshCurvatureMapEvaluator.h"
#include "Sampling/MeshMapBaker.h"
#include "MeshCurvature.h"
#include "MeshWeights.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

void FMeshCurvatureMapEvaluator::CacheDetailCurvatures(const FDynamicMesh3* DetailMeshIn)
{
	if (!Curvatures)
	{
		Curvatures = MakeShared<FMeshVertexCurvatureCache>();
		Curvatures->BuildAll(*DetailMeshIn);
	}
	check(Curvatures->Num() == DetailMeshIn->MaxVertexID());
}

void FMeshCurvatureMapEvaluator::Setup(const FMeshMapBaker& Baker, FEvaluationContext& Context)
{
	Context.Evaluate = &EvaluateSample;
	Context.EvaluateDefault = &EvaluateDefault;
	Context.EvalData = this;
	Context.AccumulateMode = EAccumulateMode::Add;
	Context.DataLayout = { EComponents::Float3 };

	// Cache data from the baker
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

void FMeshCurvatureMapEvaluator::EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData)
{
	FMeshCurvatureMapEvaluator* Eval = static_cast<FMeshCurvatureMapEvaluator*>(EvalData);
	double Curvature = Eval->SampleFunction(Sample);
	double Sign = FMathd::Sign(Curvature);
	float T = (float)Eval->ClampRange.GetT(FMathd::Abs(Curvature));
	FVector3f CurvatureColor = (Sign < 0) ?
		Lerp(Eval->ZeroColor, Eval->NegativeColor, T) : Lerp(Eval->ZeroColor, Eval->PositiveColor, T);
	WriteToBuffer(Out, CurvatureColor);
}

void FMeshCurvatureMapEvaluator::EvaluateDefault(float*& Out, void* EvalData)
{
	WriteToBuffer(Out, FVector3f::Zero());
}

double FMeshCurvatureMapEvaluator::SampleFunction(const FCorrespondenceSample& SampleData)
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

double FMeshCurvatureMapEvaluator::GetCurvature(int32 vid)
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

void FMeshCurvatureMapEvaluator::GetColorMapRange(FVector3f& NegativeColorOut, FVector3f& ZeroColorOut, FVector3f& PositiveColorOut)
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

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshPropertyMapEvaluator.h"
#include "Sampling/MeshMapBaker.h"
#include "Util/ColorConstants.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

void FMeshPropertyMapEvaluator::Setup(const FMeshMapBaker& Baker, FEvaluationContext& Context)
{
	Context.Evaluate = &EvaluateSample;
	Context.EvaluateDefault = &EvaluateDefault;
	Context.EvalData = this;
	Context.AccumulateMode = EAccumulateMode::Add;
	Context.DataLayout = { EComponents::Float3 };

	// Cache data from the baker
	DetailMesh = Baker.GetDetailMesh();
	DetailNormalOverlay = Baker.GetDetailMeshNormals();
	check(DetailNormalOverlay);
	DetailUVOverlay = Baker.GetDetailMeshUVs();

	Bounds = DetailMesh->GetBounds();
	for (int32 j = 0; j < 3; ++j)
	{
		if (Bounds.Diagonal()[j] < FMathf::ZeroTolerance)
		{
			Bounds.Min[j] = Bounds.Center()[j] - FMathf::ZeroTolerance;
			Bounds.Max[j] = Bounds.Center()[j] + FMathf::ZeroTolerance;

		}
	}

	DefaultValue = FVector3f::Zero();
	switch (this->Property)
	{
	case EMeshPropertyMapType::Position:
		DefaultValue = PositionToColor(Bounds.Center(), Bounds);
		break;
	case EMeshPropertyMapType::FacetNormal:
	case EMeshPropertyMapType::Normal:
		DefaultValue = NormalToColor(FVector3d::UnitZ());
		break;
	case EMeshPropertyMapType::UVPosition:
		DefaultValue = UVToColor(FVector2d::Zero());
		break;
	case EMeshPropertyMapType::MaterialID:
		DefaultValue = FVector3f(LinearColors::LightPink3f());
		break;
	}
}

void FMeshPropertyMapEvaluator::EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData)
{
	FMeshPropertyMapEvaluator* Eval = static_cast<FMeshPropertyMapEvaluator*>(EvalData);
	FVector3f SampleResult = Eval->SampleFunction(Sample);
	WriteToBuffer(Out, SampleResult);
}

void FMeshPropertyMapEvaluator::EvaluateDefault(float*& Out, void* EvalData)
{
	WriteToBuffer(Out, FVector3f::Zero());
}

FVector3f FMeshPropertyMapEvaluator::SampleFunction(const FCorrespondenceSample& SampleData)
{
	FVector3f Color = DefaultValue;
	int32 DetailTriID = SampleData.DetailTriID;
	if (DetailMesh->IsTriangle(DetailTriID))
	{
		switch (this->Property)
		{
		case EMeshPropertyMapType::Position:
		{
			FVector3d Position = DetailMesh->GetTriBaryPoint(DetailTriID, SampleData.DetailBaryCoords[0], SampleData.DetailBaryCoords[1], SampleData.DetailBaryCoords[2]);
			Color = PositionToColor(Position, Bounds);
		}
		break;
		case EMeshPropertyMapType::FacetNormal:
		{
			FVector3d FacetNormal = DetailMesh->GetTriNormal(DetailTriID);
			Color = NormalToColor(FacetNormal);
		}
		break;
		case EMeshPropertyMapType::Normal:
		{
			if (DetailNormalOverlay->IsSetTriangle(DetailTriID))
			{
				FVector3d DetailNormal;
				DetailNormalOverlay->GetTriBaryInterpolate<double>(DetailTriID, &SampleData.DetailBaryCoords.X, &DetailNormal.X);
				Normalize(DetailNormal);
				Color = NormalToColor(DetailNormal);
			}
		}
		break;
		case EMeshPropertyMapType::UVPosition:
		{
			if (DetailUVOverlay && DetailUVOverlay->IsSetTriangle(DetailTriID))
			{
				FVector2d DetailUV;
				DetailUVOverlay->GetTriBaryInterpolate<double>(DetailTriID, &SampleData.DetailBaryCoords.X, &DetailUV.X);
				Color = UVToColor(DetailUV);
			}
		}
		break;
		case EMeshPropertyMapType::MaterialID:
		{
			if (DetailMesh->Attributes() && DetailMesh->Attributes()->HasMaterialID())
			{
				const FDynamicMeshMaterialAttribute* DetailMaterialIDAttrib = DetailMesh->Attributes()->GetMaterialID();
				const int32 MatID = DetailMaterialIDAttrib->GetValue(DetailTriID);
				Color = LinearColors::SelectColor<FVector3f>(MatID);
			}
		}
		break;
		}
	}
	return Color;
}


// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshPropertyMapEvaluator.h"
#include "Sampling/MeshMapBaker.h"
#include "Util/ColorConstants.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

void FMeshPropertyMapEvaluator::Setup(const FMeshBaseBaker& Baker, FEvaluationContext& Context)
{
	// Cache data from the baker
	DetailMesh = Baker.GetDetailMesh();
	DetailNormalOverlay = Baker.GetDetailMeshNormals();
	check(DetailNormalOverlay);
	DetailUVOverlay = Baker.GetDetailMeshUVs();
	DetailColorOverlay = Baker.GetDetailMeshColors();
	DetailMeshTangents = Baker.GetDetailMeshTangents();
	DetailMeshNormalMap = Baker.GetDetailMeshNormalMap();

	const bool bHasDetailNormalMap = DetailMeshNormalMap != nullptr;
	Context.Evaluate = bHasDetailNormalMap ? &EvaluateSample<true> : &EvaluateSample<false>;
	Context.EvaluateDefault = &EvaluateDefault;
	Context.EvaluateColor = &EvaluateColor;
	Context.EvalData = this;
	Context.AccumulateMode = EAccumulateMode::Add;
	Context.DataLayout = { EComponents::Float3 };

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
		DefaultValue = NormalToColor(FVector3d::UnitZ());
		break;
	case EMeshPropertyMapType::Normal:
		DefaultValue = NormalToColor(FVector3d::UnitZ());
		if (bHasDetailNormalMap)
		{
			DetailUVOverlay = Baker.GetDetailMeshUVs(Baker.GetDetailMeshNormalUVLayer());
		}
		break;
	case EMeshPropertyMapType::UVPosition:
		DefaultValue = UVToColor(FVector2d::Zero());
		break;
	case EMeshPropertyMapType::MaterialID:
		DefaultValue = FVector3f(LinearColors::LightPink3f());
		break;
	case EMeshPropertyMapType::VertexColor:
		DefaultValue = FVector3f::One();
		break;
	}
}

template <bool bUseDetailNormalMap>
void FMeshPropertyMapEvaluator::EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData)
{
	FMeshPropertyMapEvaluator* Eval = static_cast<FMeshPropertyMapEvaluator*>(EvalData);
	const FVector3f SampleResult = Eval->SampleFunction<bUseDetailNormalMap>(Sample);
	WriteToBuffer(Out, SampleResult);
}

void FMeshPropertyMapEvaluator::EvaluateDefault(float*& Out, void* EvalData)
{
	WriteToBuffer(Out, FVector3f::Zero());
}

void FMeshPropertyMapEvaluator::EvaluateColor(const int DataIdx, float*& In, FVector4f& Out, void* EvalData)
{
	// TODO: Move property color space transformation from EvaluateSample/Default to here.
	Out = FVector4f(In[0], In[1], In[2], 1.0f);
	In += 3;
}

template <bool bUseDetailNormalMap>
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

				if constexpr (bUseDetailNormalMap)
				{
					FVector3d DetailTangentX, DetailTangentY;
					DetailMeshTangents->GetInterpolatedTriangleTangent(
						SampleData.DetailTriID,
						SampleData.DetailBaryCoords,
						DetailTangentX, DetailTangentY);
			
					FVector2d DetailUV;
					DetailUVOverlay->GetTriBaryInterpolate<double>(DetailTriID, &SampleData.DetailBaryCoords.X, &DetailUV.X);
					const FVector4f DetailNormalColor4 = SampleNormalMapFunction(DetailUV);

					// Map color space [0,1] to normal space [-1,1]
					const FVector3f DetailNormalColor(DetailNormalColor4.X, DetailNormalColor4.Y, DetailNormalColor4.Z);
					const FVector3f DetailNormalTangentSpace = (DetailNormalColor * 2.0f) - FVector3f::One();

					// Convert detail normal tangent space to object space
					FVector3f DetailNormalObjectSpace = DetailNormalTangentSpace.X * DetailTangentX + DetailNormalTangentSpace.Y * DetailTangentY + DetailNormalTangentSpace.Z * DetailNormal;
					Normalize(DetailNormalObjectSpace);
					DetailNormal = DetailNormalObjectSpace;
				}
				
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
		case EMeshPropertyMapType::VertexColor:
		{
			if (DetailColorOverlay && DetailColorOverlay->IsSetTriangle(DetailTriID))
			{
				FVector4d DetailColor;
				DetailColorOverlay->GetTriBaryInterpolate<double>(DetailTriID, &SampleData.DetailBaryCoords.X, &DetailColor.X);
				Color = FVector3f(DetailColor.X, DetailColor.Y, DetailColor.Z);
			}
		}
		break;
		}
	}
	return Color;
}

FVector4f FMeshPropertyMapEvaluator::SampleNormalMapFunction(const FVector2d& UVCoord) const
{
	return DetailMeshNormalMap->BilinearSampleUV<float>(UVCoord, FVector4f(0, 0, 0, 1));
}



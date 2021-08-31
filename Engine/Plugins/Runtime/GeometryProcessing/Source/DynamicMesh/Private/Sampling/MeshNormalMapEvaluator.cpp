// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshNormalMapEvaluator.h"
#include "Sampling/MeshMapBaker.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;


void FMeshNormalMapEvaluator::Setup(const FMeshBaseBaker& Baker, FEvaluationContext& Context)
{
	// Cache data from the baker
	DetailMesh = Baker.GetDetailMesh();
	DetailNormalOverlay = Baker.GetDetailMeshNormals();
	checkSlow(DetailNormalOverlay);
	const int32 DetailUVLayer = Baker.GetDetailMeshNormalUVLayer();
	DetailUVOverlay = Baker.GetDetailMeshUVs(DetailUVLayer);
	checkSlow(DetailUVOverlay);
	BaseMeshTangents = Baker.GetTargetMeshTangents();
	DetailMeshTangents = Baker.GetDetailMeshTangents();
	DetailMeshNormalMap = Baker.GetDetailMeshNormalMap();

	Context.Evaluate = DetailMeshNormalMap ? &EvaluateSample<true> : &EvaluateSample<false>;
	Context.EvaluateDefault = &EvaluateDefault;
	Context.EvaluateColor = &EvaluateColor;
	Context.EvalData = this;
	Context.AccumulateMode = EAccumulateMode::Add;
	Context.DataLayout = { EComponents::Float3 };
}

template <bool bUseDetailNormalMap>
void FMeshNormalMapEvaluator::EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData)
{
	FMeshNormalMapEvaluator* Eval = static_cast<FMeshNormalMapEvaluator*>(EvalData);
	const FVector3f SampleResult = Eval->SampleFunction<bUseDetailNormalMap>(Sample);
	WriteToBuffer(Out, SampleResult);
}

void FMeshNormalMapEvaluator::EvaluateDefault(float*& Out, void* EvalData)
{
	FMeshNormalMapEvaluator* Eval = static_cast<FMeshNormalMapEvaluator*>(EvalData);
	WriteToBuffer(Out, Eval->DefaultNormal);
}

void FMeshNormalMapEvaluator::EvaluateColor(const int DataIdx, float*& In, FVector4f& Out, void* EvalData)
{
	// Map normal space [-1,1] to color space [0,1]
	const FVector3f Normal(In[0], In[1], In[2]);
	const FVector3f Color = (Normal + FVector3f::One()) * 0.5f;
	Out = FVector4f(Color.X, Color.Y, Color.Z, 1.0f);
	In += 3;
}

template <bool bUseDetailNormalMap>
FVector3f FMeshNormalMapEvaluator::SampleFunction(const FCorrespondenceSample& SampleData) const
{
	FVector3f Result = FVector3f::UnitZ();
	const int32 DetailTriID = SampleData.DetailTriID;
	if (DetailMesh->IsTriangle(DetailTriID))
	{
		// get tangents on base mesh
		FVector3d BaseTangentX, BaseTangentY;
		BaseMeshTangents->GetInterpolatedTriangleTangent(
			SampleData.BaseSample.TriangleIndex,
			SampleData.BaseSample.BaryCoords,
			BaseTangentX, BaseTangentY);

		// sample normal on detail mesh
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

		// compute normal in tangent space
		const double dx = DetailNormal.Dot(BaseTangentX);
		const double dy = DetailNormal.Dot(BaseTangentY);
		const double dz = DetailNormal.Dot(SampleData.BaseNormal);

		Result = FVector3f((float)dx, (float)dy, (float)dz);
	}
	return Result;
}

FVector4f FMeshNormalMapEvaluator::SampleNormalMapFunction(const FVector2d& UVCoord) const
{
	return DetailMeshNormalMap->BilinearSampleUV<float>(UVCoord, FVector4f(0, 0, 0, 1));
}



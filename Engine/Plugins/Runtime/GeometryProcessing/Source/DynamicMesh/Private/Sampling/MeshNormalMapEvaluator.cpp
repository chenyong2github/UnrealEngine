// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshNormalMapEvaluator.h"
#include "Sampling/MeshMapBaker.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;


void FMeshNormalMapEvaluator::Setup(const FMeshBaseBaker& Baker, FEvaluationContext& Context)
{
	Context.Evaluate = &EvaluateSample;
	Context.EvaluateDefault = &EvaluateDefault;
	Context.EvaluateColor = &EvaluateColor;
	Context.EvalData = this;
	Context.AccumulateMode = EAccumulateMode::Add;
	Context.DataLayout = { EComponents::Float3 };

	// Cache data from the baker
	DetailMesh = Baker.GetDetailMesh();
	DetailNormalOverlay = Baker.GetDetailMeshNormals();
	checkSlow(DetailNormalOverlay);
	BaseMeshTangents = Baker.GetTargetMeshTangents().Get();
}

void FMeshNormalMapEvaluator::EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData)
{
	FMeshNormalMapEvaluator* Eval = static_cast<FMeshNormalMapEvaluator*>(EvalData);
	const FVector3f SampleResult = Eval->SampleFunction(Sample);
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

FVector3f FMeshNormalMapEvaluator::SampleFunction(const FCorrespondenceSample& SampleData) const
{
	int32 DetailTriID = SampleData.DetailTriID;
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

		// compute normal in tangent space
		double dx = DetailNormal.Dot(BaseTangentX);
		double dy = DetailNormal.Dot(BaseTangentY);
		double dz = DetailNormal.Dot(SampleData.BaseNormal);

		return FVector3f((float)dx, (float)dy, (float)dz);
	}
	return FVector3f::UnitZ();
}

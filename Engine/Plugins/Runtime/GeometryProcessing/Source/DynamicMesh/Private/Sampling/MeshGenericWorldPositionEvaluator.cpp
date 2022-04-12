// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshGenericWorldPositionEvaluator.h"
#include "Sampling/MeshBaseBaker.h"

#include "VectorTypes.h"

using namespace UE::Geometry;

void FMeshGenericWorldPositionColorEvaluator::Setup(const FMeshBaseBaker& Baker, FEvaluationContext& Context)
{
	Context.Evaluate = &EvaluateSample;
	Context.EvaluateDefault = &EvaluateDefault;
	Context.EvaluateColor = &EvaluateColor;
	Context.EvalData = this;
	Context.AccumulateMode = EAccumulateMode::Add;
	Context.DataLayout = { EComponents::Float4 };
}

void FMeshGenericWorldPositionColorEvaluator::EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData)
{
	FMeshGenericWorldPositionColorEvaluator* Eval = static_cast<FMeshGenericWorldPositionColorEvaluator*>(EvalData);
	
	const FVector3d Position = Sample.BaseSample.SurfacePoint;
	const FVector3d Normal = Sample.BaseNormal;
	const FVector4f SampleResult = Eval->ColorSampleFunction(Position, Normal);
	
	WriteToBuffer(Out, SampleResult);
}

void FMeshGenericWorldPositionColorEvaluator::EvaluateDefault(float*& Out, void* EvalData)
{
	FMeshGenericWorldPositionColorEvaluator* Eval = static_cast<FMeshGenericWorldPositionColorEvaluator*>(EvalData);
	WriteToBuffer(Out, Eval->DefaultColor);
}

void FMeshGenericWorldPositionColorEvaluator::EvaluateColor(const int DataIdx, float*& In, FVector4f& Out, void* EvalData)
{
	Out = FVector4f(In[0], In[1], In[2], In[3]);
	In += 4;
}










void FMeshGenericWorldPositionNormalEvaluator::Setup(const FMeshBaseBaker& Baker, FEvaluationContext& Context)
{
	BaseMeshTangents = Baker.GetTargetMeshTangents();
	
	// Make sure we have per-triangle tangents computed so that GetInterpolatedTriangleTangent() below works
	check(BaseMeshTangents != nullptr);
	check(BaseMeshTangents->GetTangents().Num() >= 3 * Baker.GetTargetMesh()->MaxTriangleID());

	Context.Evaluate = &EvaluateSample;
	Context.EvaluateDefault = &EvaluateDefault;
	Context.EvaluateColor = &EvaluateColor;
	Context.EvalData = this;
	Context.AccumulateMode = EAccumulateMode::Add;
	Context.DataLayout = { EComponents::Float3 };
}

void FMeshGenericWorldPositionNormalEvaluator::EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData)
{
	const FMeshGenericWorldPositionNormalEvaluator* Eval = static_cast<FMeshGenericWorldPositionNormalEvaluator*>(EvalData);
	const FVector3f SampleResult = Eval->EvaluateSampleImpl(Sample);
	WriteToBuffer(Out, SampleResult);
}

void FMeshGenericWorldPositionNormalEvaluator::EvaluateDefault(float*& Out, void* EvalData)
{
	FMeshGenericWorldPositionNormalEvaluator* Eval = static_cast<FMeshGenericWorldPositionNormalEvaluator*>(EvalData);
	WriteToBuffer(Out, Eval->DefaultUnitWorldNormal);
}

void FMeshGenericWorldPositionNormalEvaluator::EvaluateColor(const int DataIdx, float*& In, FVector4f& Out, void* EvalData)
{
	// Map normal space [-1,1] to color space [0,1]
	const FVector3f Normal(In[0], In[1], In[2]);
	const FVector3f Color = (Normal + FVector3f::One()) * 0.5f;
	Out = FVector4f(Color.X, Color.Y, Color.Z, 1.0f);
	In += 3;
}

FVector3f FMeshGenericWorldPositionNormalEvaluator::EvaluateSampleImpl(const FCorrespondenceSample& SampleData) const
{
	FVector3d Position = SampleData.BaseSample.SurfacePoint;
	FVector3d Normal = SampleData.BaseNormal;
	int32 TriangleID = SampleData.BaseSample.TriangleIndex;
	FVector3d BaryCoords = SampleData.BaseSample.BaryCoords;

	// Get tangents on base mesh
	FVector3d BaseTangentX, BaseTangentY;
	BaseMeshTangents->GetInterpolatedTriangleTangent(TriangleID, BaryCoords, BaseTangentX, BaseTangentY);

	// Sample world normal at world position
	FVector3f WorldSpaceNormal = UnitWorldNormalSampleFunction(Position, Normal);

	// Compute normal in tangent space
	FVector3f TangentSpaceNormal(
			(float)WorldSpaceNormal.Dot(FVector3f(BaseTangentX)),
			(float)WorldSpaceNormal.Dot(FVector3f(BaseTangentY)),
			(float)WorldSpaceNormal.Dot(FVector3f(SampleData.BaseNormal)));

	return TangentSpaceNormal;
}

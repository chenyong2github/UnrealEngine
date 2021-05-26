// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshResampleImageEvaluator.h"
#include "Sampling/MeshMapBaker.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

//
// FMeshResampleImage Evaluator
//

void FMeshResampleImageEvaluator::Setup(const FMeshMapBaker& Baker, FEvaluationContext& Context)
{
	Context.Evaluate = &EvaluateSample;
	Context.EvaluateDefault = &EvaluateDefault;
	Context.EvalData = this;
	Context.AccumulateMode = EAccumulateMode::Add;
	Context.DataLayout = { EComponents::Float4 };

	// Cache data from the baker
	DetailMesh = Baker.GetDetailMesh();
}

void FMeshResampleImageEvaluator::EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData)
{
	FMeshResampleImageEvaluator* Eval = static_cast<FMeshResampleImageEvaluator*>(EvalData);
	FVector4f SampleResult = Eval->ImageSampleFunction(Sample);
	WriteToBuffer(Out, SampleResult);
}

void FMeshResampleImageEvaluator::EvaluateDefault(float*& Out, void* EvalData)
{
	WriteToBuffer(Out, FVector4f(0.0f, 0.0f, 0.0f, 1.0f));
}

FVector4f FMeshResampleImageEvaluator::ImageSampleFunction(const FCorrespondenceSample& SampleData)
{
	FVector4f Color = DefaultColor;
	int32 DetailTriID = SampleData.DetailTriID;
	if (DetailMesh->IsTriangle(SampleData.DetailTriID) && DetailUVOverlay)
	{
		FVector2d DetailUV;
		DetailUVOverlay->GetTriBaryInterpolate<double>(DetailTriID, &SampleData.DetailBaryCoords.X, &DetailUV.X);
		Color = SampleFunction(DetailUV);
	}
	return Color;
}

//
// FMeshMultiResampleImage Evaluator
//

void FMeshMultiResampleImageEvaluator::Setup(const FMeshMapBaker& Baker, FEvaluationContext& Context)
{
	Context.Evaluate = &EvaluateSampleMulti;
	Context.EvaluateDefault = &EvaluateDefault;
	Context.EvalData = this;
	Context.AccumulateMode = EAccumulateMode::Add;
	Context.DataLayout = { EComponents::Float4 };

	// Cache data from baker
	DetailMesh = Baker.GetDetailMesh();
	DetailMaterialIDAttrib = ensure(DetailMesh) ? DetailMesh->Attributes()->GetMaterialID() : nullptr;
	bValidDetailMesh = ensure(DetailMaterialIDAttrib) && ensure(DetailUVOverlay);
}

void FMeshMultiResampleImageEvaluator::EvaluateSampleMulti(float*& Out, const FCorrespondenceSample& Sample, void* EvalData)
{
	FMeshMultiResampleImageEvaluator* Eval = static_cast<FMeshMultiResampleImageEvaluator*>(EvalData);
	FVector4f SampleResult = Eval->ImageSampleFunction(Sample);
	WriteToBuffer(Out, SampleResult);
}

FVector4f FMeshMultiResampleImageEvaluator::ImageSampleFunction(const FCorrespondenceSample& Sample)
{
	FVector4f Color(0.0f, 0.0f, 0.0f, 1.0f);
	if (!bValidDetailMesh)
	{
		return Color;
	}

	int32 DetailTriID = Sample.DetailTriID;
	if (DetailMesh->IsTriangle(DetailTriID))
	{
		// TODO: Decompose the Map into a sparse array lookup.
		int32 MaterialID = DetailMaterialIDAttrib->GetValue(DetailTriID);
		TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe> TextureImage = MultiTextures.FindRef(MaterialID);
		if (TextureImage)
		{
			FVector2d DetailUV;
			DetailUVOverlay->GetTriBaryInterpolate<double>(DetailTriID, &Sample.DetailBaryCoords.X, &DetailUV.X);
			Color = TextureImage->BilinearSampleUV<float>(DetailUV, FVector4f(0, 0, 0, 1));
		}
	}
	return Color;
}

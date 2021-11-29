// Copyright Epic Games, Inc. All Rights Reserved.


#include "MLPytorchDataSetInterface.h"

#include "MLDeformer.h"
#include "MLDeformerEditorData.h"
#include "MLDeformerAsset.h"
#include "MLDeformerFrameCache.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "GeometryCache.h"

UMLPytorchDataSetInterface::UMLPytorchDataSetInterface()
{
}

UMLPytorchDataSetInterface::~UMLPytorchDataSetInterface()
{
	Clear();
}

void UMLPytorchDataSetInterface::Clear()
{
	EditorData.Reset();
	FrameCache.Reset();
}

void UMLPytorchDataSetInterface::SetEditorData(TSharedPtr<FMLDeformerEditorData> InEditorData)
{
	EditorData = InEditorData;
}

void UMLPytorchDataSetInterface::SetFrameCache(TSharedPtr<FMLDeformerFrameCache> InFrameCache)
{
	FrameCache = InFrameCache;
}

bool UMLPytorchDataSetInterface::IsValid() const
{
	const FMLDeformerEditorData* Data = EditorData.Get();
	return (Data != nullptr);
}

int32 UMLPytorchDataSetInterface::GetNumberSampleTransforms() const
{
	check(IsValid());
	const FMLDeformerEditorData* Data = EditorData.Get();
	return Data->GetDeformerAsset()->GetInputInfo().GetNumBones();
}

int32 UMLPytorchDataSetInterface::GetNumberSampleCurves() const
{
	check(IsValid());
	const FMLDeformerEditorData* Data = EditorData.Get();
	return Data->GetDeformerAsset()->GetInputInfo().GetNumCurves();
}

int32 UMLPytorchDataSetInterface::GetNumberSampleDeltas() const
{
	check(IsValid());
	return UMLDeformerAsset::ExtractNumImportedGeomCacheVertices(EditorData->GetDeformerAsset()->GetGeometryCache());
}

int32 UMLPytorchDataSetInterface::NumSamples() const
{
	check(IsValid());
	return EditorData->GetDeformerAsset()->GetNumFramesForTraining();
}

bool UMLPytorchDataSetInterface::SetCurrentSampleIndex(int32 Index)
{
	check(IsValid());

	FMLDeformerEditorData* Data = EditorData.Get();
	UMLDeformerAsset* DeformerAsset = Data->GetDeformerAsset();

	// Make sure we have a valid frame number.
	if (Index < 0 || Index >= DeformerAsset->GetNumFrames())
	{
		UE_LOG(LogMLDeformer, Warning, TEXT("Sample index must range from %d to %d, but a value of %d was provided."), 0, DeformerAsset->GetNumFrames()-1, Index);
		return false;
	}

	// Generate and store the training data.
	const FMLDeformerTrainingFrame& TrainingFrame = FrameCache->GetTrainingFrameForAnimFrame(Index);
	SampleDeltas = TrainingFrame.GetVertexDeltas();
	SampleBoneRotations = TrainingFrame.GetBoneRotations();
	SampleCurveValues = TrainingFrame.GetCurveValues();

	return true;
}

bool UMLPytorchDataSetInterface::ComputeDeltasStatistics()
{
	check(IsValid());

	FMLDeformerEditorData* Data = EditorData.Get();
	UMLDeformerAsset* DeformerAsset = Data->GetDeformerAsset();

	// Generate and store the deltas.
	const float DeltaCutoffLength = DeformerAsset->GetDeltaCutoffLength();
	if (Data->ComputeVertexDeltaStatistics(0, FrameCache.Get()))
	{
		//Update mean vertex delta and vertex data scale.
		VertexDeltaMean.X = DeformerAsset->VertexDeltaMean.X;
		VertexDeltaMean.Y = DeformerAsset->VertexDeltaMean.Y;
		VertexDeltaMean.Z = DeformerAsset->VertexDeltaMean.Z;
		VertexDeltaScale.X = DeformerAsset->VertexDeltaScale.X;
		VertexDeltaScale.Y = DeformerAsset->VertexDeltaScale.Y;
		VertexDeltaScale.Z = DeformerAsset->VertexDeltaScale.Z;
		return true;
	}

	return false;
}

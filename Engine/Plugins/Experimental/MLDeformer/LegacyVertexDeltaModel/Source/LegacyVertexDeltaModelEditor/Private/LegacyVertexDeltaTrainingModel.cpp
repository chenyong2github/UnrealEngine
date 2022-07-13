// Copyright Epic Games, Inc. All Rights Reserved.

#include "LegacyVertexDeltaTrainingModel.h"
#include "LegacyVertexDeltaModel.h"
#include "LegacyVertexDeltaEditorModel.h"
#include "MLDeformerEditorModel.h"
#include "Misc/ScopedSlowTask.h"
#include "Math/NumericLimits.h"

#define LOCTEXT_NAMESPACE "LegacyVertexDeltaTrainingModel"

ULegacyVertexDeltaModel* ULegacyVertexDeltaTrainingModel::GetVertexDeltaModel() const
{
	return Cast<ULegacyVertexDeltaModel>(GetModel());
}

UE::LegacyVertexDeltaModel::FLegacyVertexDeltaEditorModel* ULegacyVertexDeltaTrainingModel::GetVertexDeltaEditorModel() const
{
	return static_cast<UE::LegacyVertexDeltaModel::FLegacyVertexDeltaEditorModel*>(EditorModel);
}

void ULegacyVertexDeltaTrainingModel::UpdateVertexDeltaMeanAndScale(const TArray<float>& VertexDeltas, FVector& InOutMeanVertexDelta, FVector& InOutVertexDeltaScale, float& InOutCount)
{
	// Calculate this frame's mean.
	FVector MeanDelta = FVector::ZeroVector;
	FVector MinDelta(TNumericLimits<float>::Max());
	FVector MaxDelta(-TNumericLimits<float>::Max());
	const int32 NumVertices = VertexDeltas.Num() / 3;
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
	{
		const int Offset = VertexIndex * 3;
		const FVector Delta(VertexDeltas[Offset], VertexDeltas[Offset+1], VertexDeltas[Offset+2]);
		MeanDelta += Delta;
		MinDelta = MinDelta.ComponentMin(Delta);
		MaxDelta = MaxDelta.ComponentMax(Delta);
	}
	const float ValidVertexCount = (float)NumVertices;
	if (NumVertices > 0)
	{
		MeanDelta /= (float)NumVertices;
	}

	// Update global mean.
	InOutCount += 1.0f;
	FVector TempDiff = MeanDelta - (FVector)InOutMeanVertexDelta;
	InOutMeanVertexDelta = InOutMeanVertexDelta + FVector(TempDiff / InOutCount);

	// Update global scale.
	TempDiff = MaxDelta - MinDelta;
	InOutVertexDeltaScale = InOutVertexDeltaScale.ComponentMax(TempDiff.GetAbs());
}

bool ULegacyVertexDeltaTrainingModel::ComputeVertexDeltaStatistics(uint32 LODIndex)
{
	using namespace UE::LegacyVertexDeltaModel;

	FLegacyVertexDeltaEditorModel* VertexDeltaEditorModel = GetVertexDeltaEditorModel();
	ULegacyVertexDeltaModel* VertexDeltaModel = VertexDeltaEditorModel->GetVertexDeltaModel();
	if (!VertexDeltaEditorModel->GetResamplingInputOutputsNeeded())
	{
		return true;
	}

	FMLDeformerSampler* Sampler = VertexDeltaEditorModel->GetSampler();
	Sampler->SetVertexDeltaSpace(EVertexDeltaSpace::PreSkinning);

	// Show some dialog with progress.
	const FText Title(LOCTEXT("PreprocessTrainingDataMessage", "Calculating data statistics"));
	const uint32 AnimNumFrames = EditorModel->GetNumFramesForTraining();
	FScopedSlowTask Task((float)AnimNumFrames, Title);
	Task.MakeDialog(true);

	// Get the base actor data.
	FVector Mean = FVector::ZeroVector;
	FVector Scale = FVector::OneVector;
	float Count = 0.0f;
	const int32 FrameCount = VertexDeltaEditorModel->GetNumFramesForTraining();
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; FrameIndex++)
	{
		// Calculate the deltas and update the mean and scale.
		Sampler->Sample(FrameIndex);
		UpdateVertexDeltaMeanAndScale(Sampler->GetVertexDeltas(), Mean, Scale, Count);

		// Forward the progress bar and check if we want to cancel.
		Task.EnterProgressFrame();
		if (Task.ShouldCancel())
		{
			return false;
		}
	}

	// Update the asset with calculated statistics.
	VertexDeltaModel->VertexDeltaMean = Mean;
	if (Count > 0.0f)
	{
		VertexDeltaModel->VertexDeltaScale = FVector::OneVector * Scale.GetMax();
		VertexDeltaEditorModel->SetResamplingInputOutputsNeeded(true);
	}

	return true;
}

bool ULegacyVertexDeltaTrainingModel::ComputeDeltasStatistics()
{
	const ULegacyVertexDeltaModel* VertexDeltaModel = GetVertexDeltaModel();
	const float DeltaCutoffLength = VertexDeltaModel->GetDeltaCutoffLength();
	if (ComputeVertexDeltaStatistics(0))
	{
		VertexDeltaMean = VertexDeltaModel->VertexDeltaMean;
		VertexDeltaScale = VertexDeltaModel->VertexDeltaScale;
		return true;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE

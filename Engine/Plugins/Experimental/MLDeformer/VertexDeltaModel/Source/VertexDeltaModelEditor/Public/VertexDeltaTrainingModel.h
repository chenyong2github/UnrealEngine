// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerTrainingModel.h"
#include "VertexDeltaTrainingModel.generated.h"

namespace UE::VertexDeltaModel
{
	class FVertexDeltaEditorModel;
}

class UVertexDeltaModel;

UCLASS(Blueprintable)
class VERTEXDELTAMODELEDITOR_API UVertexDeltaTrainingModel
	: public UMLDeformerTrainingModel
{
	GENERATED_BODY()

public:
	/** Main training function, with implementation in python. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Python")
	int32 Train() const;

	/** Compute delta statistics for the whole dataset. */
	UFUNCTION(BlueprintCallable, Category = "Training Data")
	bool ComputeDeltasStatistics();

	UVertexDeltaModel* GetVertexDeltaModel() const;
	UE::VertexDeltaModel::FVertexDeltaEditorModel* GetVertexDeltaEditorModel() const;

protected:
	bool ComputeVertexDeltaStatistics(uint32 LODIndex);
	void UpdateVertexDeltaMeanAndScale(const TArray<float>& VertexDeltas, FVector& InOutMeanVertexDelta, FVector& InOutVertexDeltaScale, float& InOutCount);

public:
	// Mean delta computed over the entire dataset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	FVector VertexDeltaMean = FVector::ZeroVector;

	// Vertex delta scale computed over the entire dataset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	FVector VertexDeltaScale = FVector::OneVector;
};

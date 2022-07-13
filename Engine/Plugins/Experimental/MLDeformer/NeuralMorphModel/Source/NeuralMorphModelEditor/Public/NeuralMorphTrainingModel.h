// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerTrainingModel.h"
#include "NeuralMorphTrainingModel.generated.h"

namespace UE::NeuralMorphModel
{
	class FNeuralMorphEditorModel;
}

class UNeuralMorphModel;

UCLASS(Blueprintable)
class NEURALMORPHMODELEDITOR_API UNeuralMorphTrainingModel
	: public UMLDeformerTrainingModel
{
	GENERATED_BODY()

public:
	/** Main training function, with implementation in python. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Training Model")
	int32 Train() const;

	UNeuralMorphModel* GetNeuralMorphModel() const;
	UE::NeuralMorphModel::FNeuralMorphEditorModel* GetNeuralMorphEditorModel() const;
};

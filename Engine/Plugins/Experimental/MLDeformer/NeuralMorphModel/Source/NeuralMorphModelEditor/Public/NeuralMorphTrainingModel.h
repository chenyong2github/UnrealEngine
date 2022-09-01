// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerTrainingModel.h"
#include "NeuralMorphTrainingModel.generated.h"

UCLASS(Blueprintable)
class NEURALMORPHMODELEDITOR_API UNeuralMorphTrainingModel
	: public UMLDeformerTrainingModel
{
	GENERATED_BODY()

public:
	/** Main training function, with implementation in python. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Training Model")
	int32 Train() const;
};

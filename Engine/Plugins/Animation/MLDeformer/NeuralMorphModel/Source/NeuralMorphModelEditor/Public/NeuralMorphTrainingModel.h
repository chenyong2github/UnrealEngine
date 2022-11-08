// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerTrainingModel.h"
#include "NeuralMorphModel.h"
#include "NeuralMorphTrainingModel.generated.h"

/**
 * The training model for the neural morph model.
 * This class is our link to the Python training.
 */
UCLASS(Blueprintable)
class NEURALMORPHMODELEDITOR_API UNeuralMorphTrainingModel
	: public UMLDeformerTrainingModel
{
	GENERATED_BODY()

public:
	/**
	 * Main training function, with implementation in python.
	 * You need to implement this method. See the UMLDeformerTrainingModel documentation for more details.
	 * @see UMLDeformerTrainingModel
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Training Model")
	int32 Train() const;

	/**
	 * Do we want to force using NNI for inference, instead of our custom inference?
	 * @result Returns true when NNI should be used for inference, or false if custom (faster) inference should be used.
	 */
	UFUNCTION(BlueprintPure, Category = "Training Data")
	bool ForceUseNNI() const
	{
		#if NEURALMORPHMODEL_FORCE_USE_NNI
			return true;
		#else
			return false;
		#endif
	}
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MLDeformerModelInstance.h"
#include "MLDeformerMorphModelInstance.generated.h"

/**
 * The model instance for the UMLDeformerMorphModel.
 * This instance will assume the neural network outputs a set of weights, one for each morph target.
 * The weights of the morph targets in the external morph target set related to the ID of the model will
 * be set to the weights that the neural network outputs.
 * The first morph target contains the means, which need to always be added to the results. Therefore the 
 * weight of the first morph target will always be forced to a value of 1.
 */
UCLASS()
class MLDEFORMERFRAMEWORK_API UMLDeformerMorphModelInstance
	: public UMLDeformerModelInstance
{
	GENERATED_BODY()

public:
	// UMLDeformerModelInstance overrides.
	virtual void RunNeuralNetwork(float ModelWeight) override;
	// ~END UMLDeformerModelInstance overrides.
};

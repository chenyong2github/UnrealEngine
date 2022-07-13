// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MLDeformerModelInstance.h"
#include "NeuralMorphModelInstance.generated.h"


UCLASS()
class NEURALMORPHMODEL_API UNeuralMorphModelInstance
	: public UMLDeformerModelInstance
{
	GENERATED_BODY()

public:
	virtual void RunNeuralNetwork(float ModelWeight) override;
};

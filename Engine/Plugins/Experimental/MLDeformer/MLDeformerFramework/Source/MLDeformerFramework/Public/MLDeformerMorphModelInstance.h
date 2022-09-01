// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MLDeformerModelInstance.h"
#include "MLDeformerMorphModelInstance.generated.h"


UCLASS()
class MLDEFORMERFRAMEWORK_API UMLDeformerMorphModelInstance
	: public UMLDeformerModelInstance
{
	GENERATED_BODY()

public:
	virtual void RunNeuralNetwork(float ModelWeight) override;
};

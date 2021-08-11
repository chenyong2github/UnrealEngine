// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralOperators/ElementWiseOperator.h"

class NEURALNETWORKINFERENCEBACKEND_API FNegOperator : public IElementWiseOperator
{
public:
	FNegOperator(const bool bIsInlinedTensor);

	virtual ~FNegOperator();

	FORCEINLINE virtual void ForwardCPU() override final;
};



/* FNegOperator inline functions
 *****************************************************************************/

void FNegOperator::ForwardCPU()
{
	ForwardCPUWithFunction([](const float InValue) { return -InValue; });
}

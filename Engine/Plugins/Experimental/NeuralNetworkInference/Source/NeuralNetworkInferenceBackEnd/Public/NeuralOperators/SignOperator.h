// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralOperators/ElementWiseOperator.h"

class NEURALNETWORKINFERENCEBACKEND_API FSignOperator : public IElementWiseOperator
{
public:
	FSignOperator(const bool bIsInlinedTensor);

	virtual ~FSignOperator();

	FORCEINLINE virtual void ForwardCPU() override final;
};



/* FSignOperator inline functions
 *****************************************************************************/

void FSignOperator::ForwardCPU()
{
	ForwardCPUWithFunction([](const float InValue) { return FMath::Sign(InValue); });
}

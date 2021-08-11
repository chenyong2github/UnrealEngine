// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralOperators/ElementWiseOperator.h"

class NEURALNETWORKINFERENCEBACKEND_API FSinOperator : public IElementWiseOperator
{
public:
	FSinOperator(const bool bIsInlinedTensor);

	virtual ~FSinOperator();

	FORCEINLINE virtual void ForwardCPU() override final;
};



/* FSinOperator inline functions
 *****************************************************************************/

void FSinOperator::ForwardCPU()
{
	ForwardCPUWithFunction([](const float InValue) { return FMath::Sin(InValue); });
}

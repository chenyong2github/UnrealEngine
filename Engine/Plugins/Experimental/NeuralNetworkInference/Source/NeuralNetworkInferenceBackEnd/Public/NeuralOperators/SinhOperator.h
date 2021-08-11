// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralOperators/ElementWiseOperator.h"

class NEURALNETWORKINFERENCEBACKEND_API FSinhOperator : public IElementWiseOperator
{
public:
	FSinhOperator(const bool bIsInlinedTensor);

	virtual ~FSinhOperator();

	FORCEINLINE virtual void ForwardCPU() override final;
};



/* FSinhOperator inline functions
 *****************************************************************************/

void FSinhOperator::ForwardCPU()
{
	ForwardCPUWithFunction([](const float InValue) { return FMath::Sinh(InValue); });
}

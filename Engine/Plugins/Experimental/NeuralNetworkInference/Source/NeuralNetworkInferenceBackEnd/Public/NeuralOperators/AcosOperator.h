// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralOperators/ElementWiseOperator.h"

class NEURALNETWORKINFERENCEBACKEND_API FAcosOperator : public IElementWiseOperator
{
public:
	FAcosOperator(const bool bIsInlinedTensor);

	virtual ~FAcosOperator();

	FORCEINLINE virtual void ForwardCPU() override final;
};



/* FAcosOperator inline functions
 *****************************************************************************/

void FAcosOperator::ForwardCPU()
{
	ForwardCPUWithFunction([](const float InValue) { return FMath::Acos(InValue); });
}

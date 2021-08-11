// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralOperators/ElementWiseOperator.h"

class NEURALNETWORKINFERENCEBACKEND_API FLogOperator : public IElementWiseOperator
{
public:
	FLogOperator(const bool bIsInlinedTensor);

	virtual ~FLogOperator();

	FORCEINLINE virtual void ForwardCPU() override final;
};



/* FLogOperator inline functions
 *****************************************************************************/

void FLogOperator::ForwardCPU()
{
	ForwardCPUWithFunction([](const float InValue) { return FMath::Loge(InValue); });
}

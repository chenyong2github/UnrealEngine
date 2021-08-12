// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ModelProto.h"
#include "NeuralOperators/ElementWiseOperator.h"

class NEURALNETWORKINFERENCE_API FLeakyReluOperator : public IElementWiseOperator
{
public:
	FLeakyReluOperator(const bool bIsInlinedTensor, const FNodeProto& InNodeProto);

	FLeakyReluOperator(const bool bIsInlinedTensor, const float InAlpha = 0.01f);

	virtual ~FLeakyReluOperator();

	FORCEINLINE virtual void ForwardCPU() override final;
};



/* FLeakyReluOperator inline functions
 *****************************************************************************/

void FLeakyReluOperator::ForwardCPU()
{
	ForwardCPUWithFunction([](const float InValue, const float InAttribute) { return (InValue < 0 ? InAttribute * InValue : InValue); });
}

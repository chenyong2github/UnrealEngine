// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralOperators/MultidirectionalBroadcastOperator.h"

class NEURALNETWORKINFERENCEBACKEND_API FSubOperator : public IMultidirectionalBroadcastOperator
{
public:
	FSubOperator(const TSet<uint32>& InPotentialInlinedTensors);

	virtual ~FSubOperator();

	FORCEINLINE virtual void ForwardCPU() override final;
};



/* FSubOperator inline functions
 *****************************************************************************/

void FSubOperator::ForwardCPU()
{
	ForwardCPUWithFunction([](const float InValueA, const float InValueB) { return InValueA - InValueB; });
}

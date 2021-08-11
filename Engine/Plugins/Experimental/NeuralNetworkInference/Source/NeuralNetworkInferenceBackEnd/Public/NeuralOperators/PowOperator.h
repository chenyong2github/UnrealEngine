// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralOperators/MultidirectionalBroadcastOperator.h"

class NEURALNETWORKINFERENCEBACKEND_API FPowOperator : public IMultidirectionalBroadcastOperator
{
public:
	FPowOperator(const TSet<uint32>& InPotentialInlinedTensors);

	virtual ~FPowOperator();

	FORCEINLINE virtual void ForwardCPU() override final;
};



/* FPowOperator inline functions
 *****************************************************************************/

void FPowOperator::ForwardCPU()
{
	ForwardCPUWithFunction(FMath::Pow);
}

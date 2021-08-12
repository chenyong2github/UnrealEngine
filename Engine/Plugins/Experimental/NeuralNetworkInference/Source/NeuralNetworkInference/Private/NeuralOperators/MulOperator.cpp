// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/MulOperator.h"



/* FMulOperator structors
 *****************************************************************************/

FMulOperator::FMulOperator(const TSet<uint32>& InPotentialInlinedTensors)
	: IMultidirectionalBroadcastOperator(TEXT("Mul"), 13, EMultidirectionalBroadcastOperator::Mul, InPotentialInlinedTensors)
{
}

FMulOperator::~FMulOperator()
{
}

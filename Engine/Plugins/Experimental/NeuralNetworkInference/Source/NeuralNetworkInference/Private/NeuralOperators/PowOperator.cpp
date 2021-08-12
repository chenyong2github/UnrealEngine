// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/PowOperator.h"



/* FPowOperator structors
 *****************************************************************************/

FPowOperator::FPowOperator(const TSet<uint32>& InPotentialInlinedTensors)
	: IMultidirectionalBroadcastOperator(TEXT("Pow"), 13, EMultidirectionalBroadcastOperator::Pow, InPotentialInlinedTensors)
{
}

FPowOperator::~FPowOperator()
{
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/AddOperator.h"



/* FAddOperator structors
 *****************************************************************************/

FAddOperator::FAddOperator(const TSet<uint32>& InPotentialInlinedTensors)
	: IMultidirectionalBroadcastOperator(TEXT("Add"), 13, EMultidirectionalBroadcastOperator::Add, InPotentialInlinedTensors)
{
}

FAddOperator::~FAddOperator()
{
}

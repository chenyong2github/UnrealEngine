// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/SubOperator.h"



/* FSubOperator structors
 *****************************************************************************/

FSubOperator::FSubOperator(const TSet<uint32>& InPotentialInlinedTensors)
	: IMultidirectionalBroadcastOperator(TEXT("Sub"), 13, EMultidirectionalBroadcastOperator::Sub, InPotentialInlinedTensors)
{
}

FSubOperator::~FSubOperator()
{
}

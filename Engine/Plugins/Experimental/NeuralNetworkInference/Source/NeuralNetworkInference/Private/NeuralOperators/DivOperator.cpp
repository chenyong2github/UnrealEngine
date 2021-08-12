// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/DivOperator.h"



/* FDivOperator structors
 *****************************************************************************/

FDivOperator::FDivOperator(const TSet<uint32>& InPotentialInlinedTensors)
	: IMultidirectionalBroadcastOperator(TEXT("Div"), 13, EMultidirectionalBroadcastOperator::Div, InPotentialInlinedTensors)
{
}

FDivOperator::~FDivOperator()
{
}

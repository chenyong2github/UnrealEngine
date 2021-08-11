// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/ReluOperator.h"



/* FReluOperator structors
 *****************************************************************************/

FReluOperator::FReluOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Relu"), 13, EElementWiseOperator::Relu, bIsInlinedTensor)
{
}

FReluOperator::~FReluOperator()
{
}

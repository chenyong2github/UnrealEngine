// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/NegOperator.h"



/* FNegOperator structors
 *****************************************************************************/

FNegOperator::FNegOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Neg"), 13, EElementWiseOperator::Neg, bIsInlinedTensor)
{
}

FNegOperator::~FNegOperator()
{
}

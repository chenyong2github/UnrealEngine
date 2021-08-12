// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/RoundOperator.h"



/* FRoundOperator structors
 *****************************************************************************/

FRoundOperator::FRoundOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Round"), 11, EElementWiseOperator::Round, bIsInlinedTensor)
{
}

FRoundOperator::~FRoundOperator()
{
}

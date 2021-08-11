// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/CeilOperator.h"



/* FCeilOperator structors
 *****************************************************************************/

FCeilOperator::FCeilOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Ceil"), 13, EElementWiseOperator::Ceil, bIsInlinedTensor)
{
}

FCeilOperator::~FCeilOperator()
{
}

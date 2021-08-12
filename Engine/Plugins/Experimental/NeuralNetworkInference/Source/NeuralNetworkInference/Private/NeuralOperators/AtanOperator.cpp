// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/AtanOperator.h"



/* FAtanOperator structors
 *****************************************************************************/

FAtanOperator::FAtanOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Atan"), 7, EElementWiseOperator::Atan, bIsInlinedTensor)
{
}

FAtanOperator::~FAtanOperator()
{
}

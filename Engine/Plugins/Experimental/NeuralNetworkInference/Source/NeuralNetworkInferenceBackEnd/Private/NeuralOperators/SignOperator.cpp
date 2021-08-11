// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/SignOperator.h"



/* FSignOperator structors
 *****************************************************************************/

FSignOperator::FSignOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Sign"), 13, EElementWiseOperator::Sign, bIsInlinedTensor)
{
}

FSignOperator::~FSignOperator()
{
}

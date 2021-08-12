// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/ExpOperator.h"



/* FExpOperator structors
 *****************************************************************************/

FExpOperator::FExpOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Exp"), 13, EElementWiseOperator::Exp, bIsInlinedTensor)
{
}

FExpOperator::~FExpOperator()
{
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/AbsOperator.h"



/* FAbsOperator structors
 *****************************************************************************/

FAbsOperator::FAbsOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Abs"), 13, EElementWiseOperator::Abs, bIsInlinedTensor)
{
}

FAbsOperator::~FAbsOperator()
{
}

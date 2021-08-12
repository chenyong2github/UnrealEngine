// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/SigmoidOperator.h"



/* FSigmoidOperator structors
 *****************************************************************************/

FSigmoidOperator::FSigmoidOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Sigmoid"), 13, EElementWiseOperator::Sigmoid, bIsInlinedTensor)
{
}

FSigmoidOperator::~FSigmoidOperator()
{
}

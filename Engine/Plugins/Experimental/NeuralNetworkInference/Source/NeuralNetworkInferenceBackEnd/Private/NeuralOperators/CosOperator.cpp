// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/CosOperator.h"



/* FCosOperator structors
 *****************************************************************************/

FCosOperator::FCosOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Cos"), 7, EElementWiseOperator::Cos, bIsInlinedTensor)
{
}

FCosOperator::~FCosOperator()
{
}

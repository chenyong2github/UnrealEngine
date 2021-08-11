// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/SinOperator.h"



/* FSinOperator structors
 *****************************************************************************/

FSinOperator::FSinOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Sin"), 7, EElementWiseOperator::Sin, bIsInlinedTensor)
{
}

FSinOperator::~FSinOperator()
{
}

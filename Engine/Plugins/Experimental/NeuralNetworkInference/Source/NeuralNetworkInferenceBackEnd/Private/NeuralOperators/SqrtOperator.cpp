// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/SqrtOperator.h"



/* FSqrtOperator structors
 *****************************************************************************/

FSqrtOperator::FSqrtOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Sqrt"), 13, EElementWiseOperator::Sqrt, bIsInlinedTensor)
{
}

FSqrtOperator::~FSqrtOperator()
{
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/SinhOperator.h"



/* FSinhOperator structors
 *****************************************************************************/

FSinhOperator::FSinhOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Sinh"), 9, EElementWiseOperator::Sinh, bIsInlinedTensor)
{
}

FSinhOperator::~FSinhOperator()
{
}

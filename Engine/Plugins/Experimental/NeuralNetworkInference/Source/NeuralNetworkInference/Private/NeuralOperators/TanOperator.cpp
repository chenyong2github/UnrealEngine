// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/TanOperator.h"



/* FTanOperator structors
 *****************************************************************************/

FTanOperator::FTanOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Tan"), 7, EElementWiseOperator::Tan, bIsInlinedTensor)
{
}

FTanOperator::~FTanOperator()
{
}

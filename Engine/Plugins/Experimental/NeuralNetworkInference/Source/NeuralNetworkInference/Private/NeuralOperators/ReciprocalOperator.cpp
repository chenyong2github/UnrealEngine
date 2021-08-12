// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/ReciprocalOperator.h"



/* FReciprocalOperator structors
 *****************************************************************************/

FReciprocalOperator::FReciprocalOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Reciprocal"), 13, EElementWiseOperator::Reciprocal, bIsInlinedTensor)
{
}

FReciprocalOperator::~FReciprocalOperator()
{
}

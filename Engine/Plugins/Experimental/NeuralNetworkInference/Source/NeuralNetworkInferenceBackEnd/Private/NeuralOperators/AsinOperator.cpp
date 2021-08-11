// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/AsinOperator.h"



/* FAsinOperator structors
 *****************************************************************************/

FAsinOperator::FAsinOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Asin"), 7, EElementWiseOperator::Asin, bIsInlinedTensor)
{
}

FAsinOperator::~FAsinOperator()
{
}

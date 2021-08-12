// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/AcosOperator.h"



/* FAcosOperator structors
 *****************************************************************************/

FAcosOperator::FAcosOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Acos"), 13, EElementWiseOperator::Acos, bIsInlinedTensor)
{
}

FAcosOperator::~FAcosOperator()
{
}

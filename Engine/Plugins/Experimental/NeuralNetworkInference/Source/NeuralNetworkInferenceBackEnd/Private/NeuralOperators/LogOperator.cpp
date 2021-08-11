// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/LogOperator.h"



/* FLogOperator structors
 *****************************************************************************/

FLogOperator::FLogOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Log"), 13, EElementWiseOperator::Log, bIsInlinedTensor)
{
}

FLogOperator::~FLogOperator()
{
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/FloorOperator.h"



/* FFloorOperator structors
 *****************************************************************************/

FFloorOperator::FFloorOperator(const bool bIsInlinedTensor)
	: IElementWiseOperator(TEXT("Floor"), 13, EElementWiseOperator::Floor, bIsInlinedTensor)
{
}

FFloorOperator::~FFloorOperator()
{
}

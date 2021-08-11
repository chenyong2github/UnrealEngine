// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralOperators/LeakyReluOperator.h"
#include "NeuralNetworkInferenceBackEndUtils.h"



/* FLeakyReluOperator structors
 *****************************************************************************/

FLeakyReluOperator::FLeakyReluOperator(const bool bIsInlinedTensor, const FNodeProto& InNodeProto)
	: FLeakyReluOperator(bIsInlinedTensor)
{
	UE_LOG(LogNeuralNetworkInferenceBackEnd, Warning, TEXT("LeakyRelu(): Constructor not tested yet."));
	if (const FAttributeProto* AlphaAttribute = FModelProto::FindElementInArray(TEXT("Alpha"), InNodeProto.Attribute, /*bMustValueBeFound*/false))
	{
		Attributes[0] = AlphaAttribute->F;
	}
}

FLeakyReluOperator::FLeakyReluOperator(const bool bIsInlinedTensor, const float InAlpha)
	: IElementWiseOperator(TEXT("LeakyRelu"), 6, EElementWiseOperator::LeakyRelu, bIsInlinedTensor, { InAlpha })
{
}

FLeakyReluOperator::~FLeakyReluOperator()
{
}

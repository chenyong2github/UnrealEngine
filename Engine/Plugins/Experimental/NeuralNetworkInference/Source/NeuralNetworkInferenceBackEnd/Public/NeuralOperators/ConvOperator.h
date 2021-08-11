// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ModelProto.h"
#include "NeuralOperators/ConvBaseOperator.h"

class NEURALNETWORKINFERENCEBACKEND_API FConvOperator : public FConvBaseOperator
{
public:
	FConvOperator(const FNodeProto& InNodeProto);

	FConvOperator(const EAutoPad InAutoPad, const TArray<int64>& InDilations, const int64 InGroup, const TArray<int64>& InKernelShape, const TArray<int64>& InPads, const TArray<int64>& InStrides);

	virtual ~FConvOperator();

protected:
	virtual bool SetAndConfigureStrides(const int32 InNumberConvolutionalDimensions) override final;
};

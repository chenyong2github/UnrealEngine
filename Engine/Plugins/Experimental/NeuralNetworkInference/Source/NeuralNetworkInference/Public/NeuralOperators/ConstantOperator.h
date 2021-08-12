// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ModelProto.h"
#include "NeuralOperator.h"

class NEURALNETWORKINFERENCE_API FConstantOperator : public FNeuralOperator
{
public:
	FConstantOperator(const FNodeProto& InNodeProto);

	FConstantOperator(const FNeuralTensor& InTensor);

	virtual ~FConstantOperator();

	virtual bool ConfigureOutputAndInternalVariablesAndSanityChecks() override final;

	virtual void ForwardCPU() override final;

	virtual void ForwardGPU_RenderThread(FRDGBuilder* InOutGraphBuilder) override final;

private:
	FNeuralTensor Tensor;
};

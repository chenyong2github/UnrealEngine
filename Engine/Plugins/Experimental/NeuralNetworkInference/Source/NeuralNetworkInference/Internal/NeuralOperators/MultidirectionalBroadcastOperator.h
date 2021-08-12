// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralInt64ArrayUInt32Buffer.h"
#include "NeuralOperator.h"
#include "NeuralOperatorEnumClasses.h"

struct FReadBuffer;

class NEURALNETWORKINFERENCE_API IMultidirectionalBroadcastOperator : public FNeuralOperator
{
public:
	/**
	 * Not inlined layer if an empty InPotentialInlinedTensors is given.
	 * It will try to inline the input tensors otherwise, where only the input tensors with the given indexes (InPotentialInlinedTensors) can be potentially inlined layers.
	 */
	IMultidirectionalBroadcastOperator(const FString& InName, const int32 InVersion, const EMultidirectionalBroadcastOperator InMultidirectionalBroadcastOperator,
		const TSet<uint32>& InPotentialInlinedTensors);

	virtual ~IMultidirectionalBroadcastOperator();

	virtual bool ConfigureOutputAndInternalVariablesAndSanityChecks() override final;

	virtual void ToGPU_RenderThread() override final;

	virtual void ForwardGPU_RenderThread(FRDGBuilder* InOutGraphBuilder) override final;

protected:
	virtual bool EstimateInlinedTensorFromPotentialOnes() override final;

	virtual void ShapesToGPU() final;

	/**
	 * This is the function that child classes must call on ForwardCPU().
	 */
	virtual void ForwardCPUWithFunction(float InOperatorFunction(const float, const float)) final;

private:
	const EMultidirectionalBroadcastOperator MultidirectionalBroadcastOperator;
	EMultidirectionalBroadcastShapeMode MultidirectionalBroadcastShapeMode;
	FNeuralInt64ArrayUInt32Buffer ShapesX;
	FNeuralInt64ArrayUInt32Buffer ShapesY;
	FNeuralInt64ArrayUInt32Buffer ShapesOutput;
};

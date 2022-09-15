// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NNXInferenceModel.h"

namespace NNX
{

/** Base class for ML model optimizers */
class IMLModelOptimizer
{
public:

	virtual ~IMLModelOptimizer() = default;

	/** Optimize model from source to destination format */
	virtual bool Optimize(TArrayView<uint8> InputModel, TArray<uint8>& OutModel) = 0;
};

/** Create a model optimizer */
NNXUTILS_API IMLModelOptimizer* CreateModelOptimizer(EMLInferenceFormat InputFormat, EMLInferenceFormat OutputFormat);

/** Create a model optimizer from ONNX to NNX */
inline IMLModelOptimizer* CreateONNXToNNXModelOptimizer()
{
	return CreateModelOptimizer(EMLInferenceFormat::ONNX, EMLInferenceFormat::NNXRT);
}

} // NNX

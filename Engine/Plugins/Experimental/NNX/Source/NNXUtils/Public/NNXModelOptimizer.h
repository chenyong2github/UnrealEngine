// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NNXRuntimeFormat.h"

namespace UE::NNECore { class FAttributeMap; }

namespace NNX
{

class IModelOptimizer;
using FOptimizerOptionsMap = UE::NNECore::FAttributeMap;
	
/** Create a model optimizer */
NNXUTILS_API TUniquePtr<IModelOptimizer> CreateModelOptimizer(ENNXInferenceFormat InputFormat, ENNXInferenceFormat OutputFormat);

inline TUniquePtr<IModelOptimizer> CreateONNXToNNXModelOptimizer()
{
	return CreateModelOptimizer(ENNXInferenceFormat::ONNX, ENNXInferenceFormat::NNXRT);
}

inline TUniquePtr<IModelOptimizer> CreateONNXToORTModelOptimizer()
{
	return CreateModelOptimizer(ENNXInferenceFormat::ONNX, ENNXInferenceFormat::ORT);
}

inline TUniquePtr<IModelOptimizer> CreateONNXToONNXModelOptimizer()
{
	return CreateModelOptimizer(ENNXInferenceFormat::ONNX, ENNXInferenceFormat::ONNX);
}

} // NNX

// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NNXRuntimeFormat.h"

namespace UE::NNECore { class FAttributeMap; }
namespace NNX { class IModelOptimizer; }

namespace UE::NNEUtils::Internal
{

using FOptimizerOptionsMap = NNECore::FAttributeMap;
	
/** Create a model optimizer */
NNEUTILS_API TUniquePtr<NNX::IModelOptimizer> CreateModelOptimizer(ENNXInferenceFormat InputFormat, ENNXInferenceFormat OutputFormat);

inline TUniquePtr<NNX::IModelOptimizer> CreateONNXToNNEModelOptimizer()
{
	return CreateModelOptimizer(ENNXInferenceFormat::ONNX, ENNXInferenceFormat::NNXRT);
}

inline TUniquePtr<NNX::IModelOptimizer> CreateONNXToORTModelOptimizer()
{
	return CreateModelOptimizer(ENNXInferenceFormat::ONNX, ENNXInferenceFormat::ORT);
}

inline TUniquePtr<NNX::IModelOptimizer> CreateONNXToONNXModelOptimizer()
{
	return CreateModelOptimizer(ENNXInferenceFormat::ONNX, ENNXInferenceFormat::ONNX);
}

} // UE::NNEUtils::Internal

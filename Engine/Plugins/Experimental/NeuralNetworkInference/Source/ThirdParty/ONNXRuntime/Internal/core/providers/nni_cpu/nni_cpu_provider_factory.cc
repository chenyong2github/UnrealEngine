// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

// #include "core/providers/nni_cpu/nni_cpu_provider_factory_creator.h"
#include "core/providers/nni_cpu/nni_cpu_provider_factory.h"

#include <memory>

#include "core/common/make_unique.h"
#include "core/providers/nni_cpu/nni_cpu_execution_provider.h"
#include "core/providers/providers.h"
#include "core/session/abi_session_options_impl.h"
#include "core/session/ort_apis.h"

namespace onnxruntime {

struct NniCpuProviderFactory : IExecutionProviderFactory {
  ~NniCpuProviderFactory() override = default;
  std::unique_ptr<IExecutionProvider> CreateProvider() override;
};

std::unique_ptr<IExecutionProvider> NniCpuProviderFactory::CreateProvider() {
  NNICPUExecutionProviderInfo info;
  return onnxruntime::make_unique<NNICPUExecutionProvider>(info);
}

std::shared_ptr<IExecutionProviderFactory> CreateExecutionProviderFactory_NNI_CPU() {
  return std::make_shared<onnxruntime::NniCpuProviderFactory>();
}

}  // namespace onnxruntime

ORT_API_STATUS_IMPL(OrtSessionOptionsAppendExecutionProvider_NNI_CPU, _In_ OrtSessionOptions* options) {
  options->provider_factories.push_back(onnxruntime::CreateExecutionProviderFactory_NNI_CPU());
  return nullptr;
}

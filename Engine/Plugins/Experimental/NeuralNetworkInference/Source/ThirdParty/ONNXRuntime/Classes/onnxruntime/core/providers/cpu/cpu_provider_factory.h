// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "onnxruntime_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \param use_arena zero: false. non-zero: true.
 */
#ifndef WITH_UE
ORT_API_STATUS(OrtSessionOptionsAppendExecutionProvider_CPU, _In_ OrtSessionOptions* options, int use_arena)
ORT_ALL_ARGS_NONNULL;
#else
UE_ORT_API_STATUS(OrtSessionOptionsAppendExecutionProvider_CPU, _In_ OrtSessionOptions* options, int use_arena)
ORT_ALL_ARGS_NONNULL;
#endif

#ifdef __cplusplus
}
#endif

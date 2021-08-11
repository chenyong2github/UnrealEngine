// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef WITH_UE

#include "onnxruntime_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \param use_arena zero: false. non-zero: true.
 */
UE_ORT_API_STATUS(OrtSessionOptionsAppendExecutionProvider_NNI_CPU, _In_ OrtSessionOptions* options)
ORT_ALL_ARGS_NONNULL;

#ifdef __cplusplus
}
#endif

#endif

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/*
Many third party headers require some care when importing. NNI third party includes should be wrapped like this:
	#include "ThirdPartyWarningDisabler.h"
	NNI_THIRD_PARTY_INCLUDES_START
	#undef check
	#undef TEXT
	// your ONNXRUNTIME include directives go here...
	NNI_THIRD_PARTY_INCLUDES_END
*/

#if PLATFORM_WINDOWS
#define NNI_THIRD_PARTY_INCLUDES_START THIRD_PARTY_INCLUDES_START \
	__pragma(warning(disable: 4191)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 4471)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 4495)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 4497)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 4530)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 4815)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 4834)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 4946)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 6001)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 6246)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 6258)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 6313)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 6387)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 6388)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 6504)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 28196)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 28204)) /* For ONNX Runtime */ \
	__pragma(warning(disable: 28205)) /* For ONNX Runtime */ \
	UE_PUSH_MACRO("check") \
	UE_PUSH_MACRO("TEXT")
#else
// If support added for other platforms, this definition may require updating
#define NNI_THIRD_PARTY_INCLUDES_START THIRD_PARTY_INCLUDES_START UE_PUSH_MACRO("check") UE_PUSH_MACRO("TEXT")
#endif //PLATFORM_WINDOWS

#define NNI_THIRD_PARTY_INCLUDES_END THIRD_PARTY_INCLUDES_END UE_POP_MACRO("check") UE_POP_MACRO("TEXT")

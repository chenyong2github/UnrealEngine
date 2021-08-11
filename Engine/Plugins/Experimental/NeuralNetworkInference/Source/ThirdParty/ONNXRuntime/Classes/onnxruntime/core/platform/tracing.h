// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#if defined(WITH_UE)
#include "ThirdPartyWarningDisabler.h"
NNI_THIRD_PARTY_INCLUDES_START
#undef TEXT
#undef check
#include <Windows.h>
#include <TraceLoggingProvider.h>
NNI_THIRD_PARTY_INCLUDES_END
#else
#include <Windows.h>
#include <TraceLoggingProvider.h>
#endif

TRACELOGGING_DECLARE_PROVIDER(telemetry_provider_handle);

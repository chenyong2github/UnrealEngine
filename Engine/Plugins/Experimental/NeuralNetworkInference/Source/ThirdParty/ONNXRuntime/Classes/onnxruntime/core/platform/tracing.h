// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#ifdef WITH_UE
#include "ThirdPartyWarningDisabler.h"
NNI_THIRD_PARTY_INCLUDES_START
#undef TEXT
#undef check
#include <windows.h>
#include <TraceLoggingProvider.h>
NNI_THIRD_PARTY_INCLUDES_END
#else //WITH_UE
#include <windows.h>
#include <TraceLoggingProvider.h>
#endif //WITH_UE

TRACELOGGING_DECLARE_PROVIDER(telemetry_provider_handle);

// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "NNXThirdPartyWarningDisabler.h" // WITH_UE
NNX_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#include <windows.h>
NNX_THIRD_PARTY_INCLUDES_END // WITH_UE
#include <TraceLoggingProvider.h>

TRACELOGGING_DECLARE_PROVIDER(telemetry_provider_handle);

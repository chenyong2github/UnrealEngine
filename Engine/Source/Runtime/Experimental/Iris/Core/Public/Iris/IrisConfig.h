// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace UE::Net
{

IRISCORE_API bool ShouldUseIrisReplication();
IRISCORE_API void SetUseIrisReplication(bool EnableIrisReplication);

}

/** Allow multiple replication systems inside the same process. Ex: PIE support */
#ifndef UE_NET_ALLOW_MULTIPLE_REPLICATION_SYSTEMS
#if (WITH_EDITOR || WITH_DEV_AUTOMATION_TESTS || WITH_AUTOMATION_WORKER)
#	define UE_NET_ALLOW_MULTIPLE_REPLICATION_SYSTEMS 1
#else
#	define UE_NET_ALLOW_MULTIPLE_REPLICATION_SYSTEMS 0
#endif
#endif

/* NetBitStreamReader/Writer validation support */
#ifndef UE_NETBITSTREAMWRITER_VALIDATE
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#define UE_NETBITSTREAMWRITER_VALIDATE 1
#else
#define UE_NETBITSTREAMWRITER_VALIDATE 0
#endif
#endif



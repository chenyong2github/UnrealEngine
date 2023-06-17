// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include <atomic>

#if COUNTERSTRACE_ENABLED || CSV_PROFILER
#	define IAS_WITH_STATISTICS 1
#else
#	define IAS_WITH_STATISTICS 0
#endif

namespace UE::IO::Private
{

#if IAS_WITH_STATISTICS
#	define IAS_STATISTICS_IMPL() ;
#else
#	define IAS_STATISTICS_IMPL() {}
#endif

class FOnDemandIoBackendStats
{
public:
	void OnIoRequestEnqueue() IAS_STATISTICS_IMPL()
	void OnIoRequestComplete(uint64 RequestSize) IAS_STATISTICS_IMPL()
	void OnIoRequestCancel() IAS_STATISTICS_IMPL()
	void OnIoRequestFail() IAS_STATISTICS_IMPL()
	void OnChunkRequestCreate() IAS_STATISTICS_IMPL()
	void OnChunkRequestRelease() IAS_STATISTICS_IMPL()
	void OnCacheHit(uint64 InSize) IAS_STATISTICS_IMPL()
	void OnCachePut(uint64 InSize) IAS_STATISTICS_IMPL()
	void OnCacheReject(uint64 InSize) IAS_STATISTICS_IMPL()
	void OnHttpRequestEnqueue() IAS_STATISTICS_IMPL()
	void OnHttpRequestDequeue() IAS_STATISTICS_IMPL()
	void OnHttpRequestComplete(uint64 InSize) IAS_STATISTICS_IMPL()
	void OnHttpRequestFail() IAS_STATISTICS_IMPL()
};

#undef IAS_STATISTICS_IMPL

} // namespace UE::IO::Private

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

class FOnDemandIoBackendStats
{
public:
	void OnIoRequestEnqueue();
	void OnIoRequestComplete(uint64 RequestSize);
	void OnIoRequestCancel();
	void OnIoRequestFail();
	void OnChunkRequestCreate();
	void OnChunkRequestRelease();
	void OnCacheHit(uint64 InSize);
	void OnCachePut(uint64 InSize);
	void OnCacheReject(uint64 InSize);
	void OnHttpRequestEnqueue();
	void OnHttpRequestDequeue();
	void OnHttpRequestComplete(uint64 InSize);
	void OnHttpRequestFail();

private:
	int32 IoRequestsMade = 0;
	std::atomic<int32> IoRequestsCompleted = 0;
	int32 IoRequestsCancelled = 0;
	std::atomic<int32> IoRequestsFailed = 0;
	int32 ReadRequestsCreated = 0;
	std::atomic<int32> ReadRequestsRemoved = 0;
	std::atomic<int32> CacheHits = 0;
	std::atomic<int32> CachePuts = 0;
	std::atomic<int32> CacheRejects = 0;
	int32 HttpRequestsCompleted = 0;
	int32 HttpRequestsFailed = 0;
	std::atomic<int32> HttpRequestsPending = 0;
	int32 HttpRequestsInflight = 0;

	std::atomic<uint64> IoRequestsCompletedSize = 0;
	std::atomic<uint64> CacheHitsSize = 0;
	std::atomic<uint64> CachePutsSize = 0;
	std::atomic<uint64> CacheRejectsSize = 0;
	uint64 HttpRequestsCompletedSize = 0;

	float BytesToApproxMB(uint64 Bytes)
	{
		return float(double(Bytes) / 1024.0 / 1024.0);
	}

	float BytesToApproxKB(uint64 Bytes)
	{
		return float(double(Bytes) / 1024.0);
	}
};

#else // IAS_WITH_STATISTICS

class FOnDemandIoBackendStats
{
public:
	void OnIoRequestEnqueue() {}
	void OnIoRequestComplete(uint64 RequestSize) {}
	void OnIoRequestCancel() {}
	void OnIoRequestFail() {}
	void OnChunkRequestCreate() {}
	void OnChunkRequestRelease() {}
	void OnCacheHit(uint64 InSize) {}
	void OnCachePut(uint64 InSize) {}
	void OnCacheReject(uint64 InSize) {}
	void OnHttpRequestEnqueue() {}
	void OnHttpRequestDequeue() {}
	void OnHttpRequestComplete(uint64 InSize) {}
	void OnHttpRequestFail() {}
};

#endif // IAS_WITH_STATISTICS

} // namespace UE::IO::Private

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Statistics.h"

#if IAS_WITH_STATISTICS

namespace UE::IO::Private
{

///////////////////////////////////////////////////////////////////////////////
// TRACE STATS
// iorequest stats
TRACE_DECLARE_INT_COUNTER(IoRequestsMade, TEXT("Ias/IoRequestsMade"));
TRACE_DECLARE_INT_COUNTER(IoRequestsCompleted, TEXT("Ias/IoRequestsCompleted"));
TRACE_DECLARE_MEMORY_COUNTER(IoRequestsCompletedSize, TEXT("Ias/Size/IoRequestsCompletedSize"));
TRACE_DECLARE_INT_COUNTER(IoRequestsCancelled, TEXT("Ias/IoRequestsCancelled"));
TRACE_DECLARE_INT_COUNTER(IoRequestsFailed, TEXT("Ias/IoRequestsFailed"));
// chunkrequest stats
TRACE_DECLARE_INT_COUNTER(ReadRequestsCreated, TEXT("Ias/ReadRequestsCreated"));
TRACE_DECLARE_INT_COUNTER(ReadRequestsRemoved, TEXT("Ias/ReadRequestsRemoved"));
// cache stats
TRACE_DECLARE_INT_COUNTER(CacheHits, TEXT("Ias/CacheHits"));
TRACE_DECLARE_MEMORY_COUNTER(CacheHitsSize, TEXT("Ias/Size/CacheHitsSize"));
TRACE_DECLARE_INT_COUNTER(CachePuts, TEXT("Ias/CachePuts"));
TRACE_DECLARE_MEMORY_COUNTER(CachePutsSize, TEXT("Ias/Size/CachePutsSize"));
TRACE_DECLARE_INT_COUNTER(CacheRejects, TEXT("Ias/CacheRejects"));
TRACE_DECLARE_MEMORY_COUNTER(CacheRejectsSize, TEXT("Ias/Size/CacheRejectsSize"));
// http stats
TRACE_DECLARE_INT_COUNTER(HttpRequestsCompleted, TEXT("Ias/HttpRequestsCompleted"));
TRACE_DECLARE_INT_COUNTER(HttpRequestsFailed, TEXT("Ias/HttpRequestsFailed"));
TRACE_DECLARE_INT_COUNTER(HttpRequestsPending, TEXT("Ias/HttpRequestsPending"));
TRACE_DECLARE_INT_COUNTER(HttpRequestsInflight, TEXT("Ias/HttpRequestsInflight"));
TRACE_DECLARE_MEMORY_COUNTER(HttpRequestsCompletedSize, TEXT("Ias/Size/HttpRequestsCompletedSize"));
///////////////////////////////////////////////////////////////////////////////
// CSV STATS
CSV_DEFINE_CATEGORY(Ias, true);
// iorequest stats
CSV_DEFINE_STAT(Ias, FrameIoRequestsMade);
CSV_DEFINE_STAT(Ias, FrameIoRequestsCompleted);
CSV_DEFINE_STAT(Ias, FrameIoRequestsCompletedSize);
CSV_DEFINE_STAT(Ias, FrameIoRequestsCancelled);
CSV_DEFINE_STAT(Ias, FrameIoRequestsFailed);
// chunkrequest stats
CSV_DEFINE_STAT(Ias, FrameReadRequestsCreated);
CSV_DEFINE_STAT(Ias, FrameReadRequestsRemoved);
// cache stats
CSV_DEFINE_STAT(Ias, FrameCacheHits);
CSV_DEFINE_STAT(Ias, FrameCacheHitsSize);
CSV_DEFINE_STAT(Ias, FrameCachePuts);
CSV_DEFINE_STAT(Ias, FrameCachePutsSize);
CSV_DEFINE_STAT(Ias, FrameCacheRejects);
CSV_DEFINE_STAT(Ias, FrameCacheRejectsSize);
// http stats
CSV_DEFINE_STAT(Ias, FrameHttpRequestsCompleted);
CSV_DEFINE_STAT(Ias, FrameHttpRequestsFailed);
CSV_DEFINE_STAT(Ias, FrameHttpRequestsPending);
CSV_DEFINE_STAT(Ias, FrameHttpRequestsInflight);
CSV_DEFINE_STAT(Ias, FrameHttpRequestsCompletedSize);

void FOnDemandIoBackendStats::OnIoRequestEnqueue()
{
	++IoRequestsMade;
	TRACE_COUNTER_SET(IoRequestsMade, IoRequestsMade);
	CSV_CUSTOM_STAT_DEFINED(FrameIoRequestsMade, IoRequestsMade, ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnIoRequestComplete(uint64 RequestSize)
{
	int32 Count = IoRequestsCompleted.fetch_add(1, std::memory_order_relaxed);
	TRACE_COUNTER_SET(IoRequestsCompleted, Count);
	CSV_CUSTOM_STAT_DEFINED(FrameIoRequestsCompleted, Count, ECsvCustomStatOp::Set);

	uint64 Size = IoRequestsCompletedSize.fetch_add(RequestSize);
	TRACE_COUNTER_SET(IoRequestsCompletedSize, Size);
	CSV_CUSTOM_STAT_DEFINED(FrameIoRequestsCompletedSize, BytesToApproxKB(Size), ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnIoRequestCancel()
{
	++IoRequestsCancelled;
	TRACE_COUNTER_SET(IoRequestsCancelled, IoRequestsCancelled);
	CSV_CUSTOM_STAT_DEFINED(FrameIoRequestsCancelled, IoRequestsCancelled, ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnIoRequestFail()
{
	int32 Count = IoRequestsFailed.fetch_add(1, std::memory_order_relaxed);
	TRACE_COUNTER_SET(IoRequestsFailed, Count);
	CSV_CUSTOM_STAT_DEFINED(FrameIoRequestsFailed, Count, ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnChunkRequestCreate()
{
	++ReadRequestsCreated;
	TRACE_COUNTER_SET(ReadRequestsCreated, ReadRequestsCreated);
	CSV_CUSTOM_STAT_DEFINED(FrameReadRequestsCreated, ReadRequestsCreated, ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnChunkRequestRelease()
{
	int32 Count = ReadRequestsRemoved.fetch_add(1, std::memory_order_relaxed);
	TRACE_COUNTER_SET(ReadRequestsRemoved, Count);
	CSV_CUSTOM_STAT_DEFINED(FrameReadRequestsRemoved, Count, ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnCacheHit(uint64 InSize)
{
	int32 Count = CacheHits.fetch_add(1, std::memory_order_relaxed);
	TRACE_COUNTER_SET(CacheHits, Count);
	CSV_CUSTOM_STAT_DEFINED(FrameCacheHits, Count, ECsvCustomStatOp::Set);

	uint64 Size = CacheHitsSize.fetch_add(InSize, std::memory_order_relaxed);
	TRACE_COUNTER_SET(CacheHitsSize, Size);
	CSV_CUSTOM_STAT_DEFINED(FrameCacheHitsSize, BytesToApproxKB(Size), ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnCachePut(uint64 InSize)
{
	int32 Count = CachePuts.fetch_add(1, std::memory_order_relaxed);
	TRACE_COUNTER_SET(CachePuts, Count);
	CSV_CUSTOM_STAT_DEFINED(FrameCachePuts, Count, ECsvCustomStatOp::Set);

	uint64 Size = CachePutsSize.fetch_add(InSize, std::memory_order_relaxed);
	TRACE_COUNTER_SET(CachePutsSize, Size);
	CSV_CUSTOM_STAT_DEFINED(FrameCachePutsSize, BytesToApproxKB(Size), ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnCacheReject(uint64 InSize)
{
	int32 Count = CacheRejects.fetch_add(1, std::memory_order_relaxed);
	TRACE_COUNTER_SET(CacheRejects, Count);
	CSV_CUSTOM_STAT_DEFINED(FrameCacheRejects, Count, ECsvCustomStatOp::Set);

	uint64 Size = CacheRejectsSize.fetch_add(InSize, std::memory_order_relaxed);
	TRACE_COUNTER_SET(CacheRejectsSize, Size);
	CSV_CUSTOM_STAT_DEFINED(FrameCacheRejectsSize, BytesToApproxKB(Size), ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnHttpRequestEnqueue()
{
	int32 Pending = HttpRequestsPending.fetch_add(1, std::memory_order_relaxed);
	TRACE_COUNTER_SET(HttpRequestsPending, Pending);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsPending, Pending, ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnHttpRequestDequeue()
{
	int32 Pending = HttpRequestsPending.fetch_sub(1, std::memory_order_relaxed);
	TRACE_COUNTER_SET(HttpRequestsPending, Pending);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsPending, Pending, ECsvCustomStatOp::Set);

	int32 Inflight = ++HttpRequestsInflight;
	TRACE_COUNTER_SET(HttpRequestsInflight, Inflight);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsPending, Inflight, ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnHttpRequestComplete(uint64 InSize)
{
	int32 Inflight = --HttpRequestsInflight;
	TRACE_COUNTER_SET(HttpRequestsInflight, Inflight);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsPending, Inflight, ECsvCustomStatOp::Set);

	++HttpRequestsCompleted;
	TRACE_COUNTER_SET(HttpRequestsCompleted, HttpRequestsCompleted);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsCompleted, HttpRequestsCompleted, ECsvCustomStatOp::Set);

	HttpRequestsCompletedSize += InSize;
	TRACE_COUNTER_SET(HttpRequestsCompletedSize, HttpRequestsCompletedSize);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsCompletedSize, BytesToApproxKB(HttpRequestsCompletedSize), ECsvCustomStatOp::Set);
}
void FOnDemandIoBackendStats::OnHttpRequestFail()
{
	int32 Inflight = --HttpRequestsInflight;
	TRACE_COUNTER_SET(HttpRequestsInflight, Inflight);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsPending, Inflight, ECsvCustomStatOp::Set);

	++HttpRequestsFailed;
	TRACE_COUNTER_SET(HttpRequestsFailed, HttpRequestsFailed);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsFailed, HttpRequestsFailed, ECsvCustomStatOp::Set);

}

} // namespace UE::IO::Private

#endif // IAS_WITH_STATISTICS

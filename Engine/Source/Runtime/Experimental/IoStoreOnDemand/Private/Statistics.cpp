// Copyright Epic Games, Inc. All Rights Reserved.

#include "Statistics.h"

#if IAS_WITH_STATISTICS

namespace UE::IO::Private
{

////////////////////////////////////////////////////////////////////////////////
static int32 BytesToApproxMB(uint64 Bytes) { return int32(Bytes >> 20); }
static int32 BytesToApproxKB(uint64 Bytes) { return int32(Bytes >> 10); }

////////////////////////////////////////////////////////////////////////////////
// TRACE STATS

#if COUNTERSTRACE_ENABLED
	using FCounterInt		= FCountersTrace::FCounterInt;
	using FCounterAtomicInt = FCountersTrace::FCounterAtomicInt;
#else
	template <typename Type>
	struct TCounterInt
	{
		TCounterInt(...)  {}
		void Set(int64 i) { V = i; }
		void Add(int64 d) { V += d; }
		int64 Get() const { return V;}
		Type V = 0;
	};
	using FCounterInt		= TCounterInt<int64>;
	using FCounterAtomicInt = TCounterInt<std::atomic<int64>>;
#endif 

// iorequest stats
FCounterInt GIoRequestsMade(TEXT("Ias/IoRequestsMade"), TraceCounterDisplayHint_None);
FCounterInt GIoRequestsCompleted(TEXT("Ias/IoRequestsCompleted"), TraceCounterDisplayHint_None);
FCounterInt GIoRequestsCompletedSize(TEXT("Ias/Size/IoRequestsCompletedSize"), TraceCounterDisplayHint_Memory);
FCounterInt GIoRequestsCancelled(TEXT("Ias/IoRequestsCancelled"), TraceCounterDisplayHint_None);
FCounterInt GIoRequestsFailed(TEXT("Ias/IoRequestsFailed"), TraceCounterDisplayHint_None);
// chunkrequest stats
FCounterInt GReadRequestsCreated(TEXT("Ias/ReadRequestsCreated"), TraceCounterDisplayHint_None);
FCounterInt GReadRequestsRemoved(TEXT("Ias/ReadRequestsRemoved"), TraceCounterDisplayHint_None);
// cache stats
FCounterInt GCacheHits(TEXT("Ias/CacheHits"), TraceCounterDisplayHint_None);
FCounterInt GCacheHitsSize(TEXT("Ias/Size/CacheHitsSize"), TraceCounterDisplayHint_Memory);
FCounterInt GCachePuts(TEXT("Ias/CachePuts"), TraceCounterDisplayHint_None);
FCounterInt GCachePutsSize(TEXT("Ias/Size/CachePutsSize"), TraceCounterDisplayHint_Memory);
FCounterInt GCacheRejects(TEXT("Ias/CacheRejects"), TraceCounterDisplayHint_None);
FCounterInt GCacheRejectsSize(TEXT("Ias/Size/CacheRejectsSize"), TraceCounterDisplayHint_Memory);
// http stats
FCounterInt GHttpRequestsCompleted(TEXT("Ias/HttpRequestsCompleted"), TraceCounterDisplayHint_None);
FCounterInt GHttpRequestsFailed(TEXT("Ias/HttpRequestsFailed"), TraceCounterDisplayHint_None);
FCounterInt GHttpRequestsPending(TEXT("Ias/HttpRequestsPending"), TraceCounterDisplayHint_None);
FCounterInt GHttpRequestsInflight(TEXT("Ias/HttpRequestsInflight"), TraceCounterDisplayHint_None);
FCounterInt GHttpRequestsCompletedSize(TEXT("Ias/Size/HttpRequestsCompletedSize"), TraceCounterDisplayHint_Memory);

////////////////////////////////////////////////////////////////////////////////
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
	GIoRequestsMade.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameIoRequestsMade, int32(GIoRequestsMade.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnIoRequestComplete(uint64 RequestSize)
{
	GIoRequestsCompleted.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameIoRequestsCompleted, int32(GIoRequestsCompleted.Get()), ECsvCustomStatOp::Set);

	GIoRequestsCompletedSize.Add(RequestSize);
	CSV_CUSTOM_STAT_DEFINED(FrameIoRequestsCompletedSize, BytesToApproxKB(GIoRequestsCompletedSize.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnIoRequestCancel()
{
	GIoRequestsCancelled.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameIoRequestsCancelled, int32(GIoRequestsCancelled.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnIoRequestFail()
{
	GIoRequestsFailed.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameIoRequestsFailed, int32(GIoRequestsFailed.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnChunkRequestCreate()
{
	GReadRequestsCreated.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameReadRequestsCreated, int32(GReadRequestsCreated.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnChunkRequestRelease()
{
	GReadRequestsRemoved.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameReadRequestsRemoved, int32(GReadRequestsRemoved.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnCacheHit(uint64 InSize)
{
	GCacheHits.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameCacheHits, int32(GCacheHits.Get()), ECsvCustomStatOp::Set);

	GCacheHitsSize.Add(InSize);
	CSV_CUSTOM_STAT_DEFINED(FrameCacheHitsSize, BytesToApproxKB(GCacheHitsSize.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnCachePut(uint64 InSize)
{
	GCachePuts.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameCachePuts, int32(GCachePuts.Get()), ECsvCustomStatOp::Set);

	GCachePutsSize.Add(InSize);
	CSV_CUSTOM_STAT_DEFINED(FrameCachePutsSize, BytesToApproxKB(GCachePutsSize.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnCacheReject(uint64 InSize)
{
	GCacheRejects.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameCacheRejects, int32(GCacheRejects.Get()), ECsvCustomStatOp::Set);

	GCacheRejectsSize.Add(InSize);
	CSV_CUSTOM_STAT_DEFINED(FrameCacheRejectsSize, BytesToApproxKB(GCacheRejectsSize.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnHttpRequestEnqueue()
{
	GHttpRequestsPending.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsPending, int32(GHttpRequestsPending.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnHttpRequestDequeue()
{
	GHttpRequestsPending.Add(-1);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsPending, int32(GHttpRequestsPending.Get()), ECsvCustomStatOp::Set);

	GHttpRequestsInflight.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsPending, int32(GHttpRequestsInflight.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnHttpRequestComplete(uint64 InSize)
{
	GHttpRequestsInflight.Add(-1);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsPending, int32(GHttpRequestsInflight.Get()), ECsvCustomStatOp::Set);

	GHttpRequestsCompleted.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsCompleted, int32(GHttpRequestsCompleted.Get()), ECsvCustomStatOp::Set);

	GHttpRequestsCompletedSize.Add(InSize);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsCompletedSize, BytesToApproxKB(GHttpRequestsCompletedSize.Get()), ECsvCustomStatOp::Set);
}

void FOnDemandIoBackendStats::OnHttpRequestFail()
{
	GHttpRequestsInflight.Add(-1);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsPending, int32(GHttpRequestsInflight.Get()), ECsvCustomStatOp::Set);

	GHttpRequestsFailed.Add(1);
	CSV_CUSTOM_STAT_DEFINED(FrameHttpRequestsFailed, int32(GHttpRequestsFailed.Get()), ECsvCustomStatOp::Set);
}

} // namespace UE::IO::Private

#endif // IAS_WITH_STATISTICS

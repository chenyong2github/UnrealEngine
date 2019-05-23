// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Common/SlabAllocator.h"
#include "Common/PagedArray.h"
#include "Common/StringStore.h"
#include "Model/MonotonicTimeline.h"
#include "Model/Tables.h"

namespace Trace
{

class FAnalysisSessionLock;

class FLoadTimeProfilerProvider
	: public ILoadTimeProfilerProvider
{
public:
	typedef TMonotonicTimeline<FLoadTimeProfilerCpuEvent> CpuTimelineInternal;

	FLoadTimeProfilerProvider(FSlabAllocator& Allocator, FAnalysisSessionLock& InSessionLock, FStringStore& StringStore);
	virtual uint64 GetPackageCount() const override { return Packages.Num(); }
	virtual void EnumeratePackages(TFunctionRef<void(const FPackageInfo&)> Callback) const override;
	virtual void ReadMainThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const override;
	virtual void ReadAsyncLoadingThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const override;
	virtual ITable<FLoadTimeProfilerAggregatedStats>* CreateEventAggregation(double IntervalStart, double IntervalEnd) const override;
	virtual ITable<FLoadTimeProfilerAggregatedStats>* CreateObjectTypeAggregation(double IntervalStart, double IntervalEnd) const override;
	const FClassInfo& AddClassInfo(const TCHAR* ClassName);
	FPackageInfo& CreatePackage(const TCHAR* PackageName);
	FPackageExportInfo& CreateExport();
	TSharedRef<CpuTimelineInternal> EditMainThreadCpuTimeline() { return MainThreadCpuTimeline; }
	TSharedRef<CpuTimelineInternal> EditAsyncLoadingThreadCpuTimeline() { return AsyncLoadingThreadCpuTimeline; }

private:
	FSlabAllocator& Allocator;
	FAnalysisSessionLock& SessionLock;
	FStringStore& StringStore;
	TPagedArray<FClassInfo> ClassInfos;
	TPagedArray<FPackageInfo> Packages;
	TPagedArray<FPackageExportInfo> Exports;
	TSharedRef<CpuTimelineInternal> MainThreadCpuTimeline;
	TSharedRef<CpuTimelineInternal> AsyncLoadingThreadCpuTimeline;

	UE_TRACE_TABLE_LAYOUT_BEGIN(FAggregatedStatsTableLayout, FLoadTimeProfilerAggregatedStats)
		UE_TRACE_TABLE_COLUMN(Name, TEXT("Name"))
		UE_TRACE_TABLE_COLUMN(Count, TEXT("Count"))
		UE_TRACE_TABLE_COLUMN(Total, TEXT("Total"))
		UE_TRACE_TABLE_COLUMN(Min, TEXT("Min"))
		UE_TRACE_TABLE_COLUMN(Max, TEXT("Max"))
		UE_TRACE_TABLE_COLUMN(Average, TEXT("Avg"))
		UE_TRACE_TABLE_COLUMN(Median, TEXT("Med"))
	UE_TRACE_TABLE_LAYOUT_END()
};

}

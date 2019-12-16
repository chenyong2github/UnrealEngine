// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/LoadTimeProfiler.h"
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

	FLoadTimeProfilerProvider(IAnalysisSession& Session);
	virtual void ReadMainThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const override;
	virtual void ReadAsyncLoadingThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const override;
	virtual ITable<FLoadTimeProfilerAggregatedStats>* CreateEventAggregation(double IntervalStart, double IntervalEnd) const override;
	virtual ITable<FLoadTimeProfilerAggregatedStats>* CreateObjectTypeAggregation(double IntervalStart, double IntervalEnd) const override;
	virtual ITable<FPackagesTableRow>* CreatePackageDetailsTable(double IntervalStart, double IntervalEnd) const override;
	virtual ITable<FExportsTableRow>* CreateExportDetailsTable(double IntervalStart, double IntervalEnd) const override;
	virtual const ITable<FLoadRequest>& GetRequestsTable() const override { return RequestsTable; }
	const FClassInfo& AddClassInfo(const TCHAR* ClassName);
	FLoadRequest& CreateRequest();
	FPackageInfo& CreatePackage(const TCHAR* PackageName);
	FPackageExportInfo& CreateExport();
	CpuTimelineInternal& EditMainThreadCpuTimeline() { return MainThreadCpuTimeline.Get(); }
	CpuTimelineInternal& EditAsyncLoadingThreadCpuTimeline() { return AsyncLoadingThreadCpuTimeline.Get(); }
	CpuTimelineInternal& EditAdditionalCpuTimeline(uint32 ThreadId);
	virtual uint32 GetMainThreadId() const override { return MainThreadId; }
	void SetMainThreadId(uint32 ThreadId) { MainThreadId = ThreadId; }
	virtual uint32 GetAsyncLoadingThreadId() const override { return AsyncLoadingThreadId; }
	void SetAsyncLoadingThreadId(uint32 ThreadId) { AsyncLoadingThreadId = ThreadId; }

private:
	static uint64 PackageSizeSum(const FLoadRequest& Row);

	IAnalysisSession& Session;
	TPagedArray<FClassInfo> ClassInfos;
	TPagedArray<FLoadRequest> Requests;
	TPagedArray<FPackageInfo> Packages;
	TPagedArray<FPackageExportInfo> Exports;
	TSharedRef<CpuTimelineInternal> MainThreadCpuTimeline;
	TSharedRef<CpuTimelineInternal> AsyncLoadingThreadCpuTimeline;
	TMap<uint32, TSharedRef<CpuTimelineInternal>> AdditionalCpuTimelinesMap;
	uint32 MainThreadId = uint32(-1);
	uint32 AsyncLoadingThreadId = uint32(-1);
	TTableView<FLoadRequest> RequestsTable;
	TTableLayout<FLoadTimeProfilerAggregatedStats> AggregatedStatsTableLayout;
	TTableLayout<FPackagesTableRow> PackagesTableLayout;
	TTableLayout<FExportsTableRow> ExportsTableLayout;
};

}

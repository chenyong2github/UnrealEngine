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

	UE_TRACE_TABLE_LAYOUT_BEGIN(FAggregatedStatsTableLayout, FLoadTimeProfilerAggregatedStats)
		UE_TRACE_TABLE_COLUMN(Name, TEXT("Name"))
		UE_TRACE_TABLE_COLUMN(Count, TEXT("Count"))
		UE_TRACE_TABLE_COLUMN(Total, TEXT("Total"))
		UE_TRACE_TABLE_COLUMN(Min, TEXT("Min"))
		UE_TRACE_TABLE_COLUMN(Max, TEXT("Max"))
		UE_TRACE_TABLE_COLUMN(Average, TEXT("Avg"))
		UE_TRACE_TABLE_COLUMN(Median, TEXT("Med"))
	UE_TRACE_TABLE_LAYOUT_END()

	UE_TRACE_TABLE_LAYOUT_BEGIN(FPackagesTableLayout, FPackagesTableRow)
		UE_TRACE_TABLE_PROJECTED_COLUMN(TableColumnType_CString, TEXT("Package"), [](const FPackagesTableRow& Row) { return Row.PackageInfo->Name; })
		UE_TRACE_TABLE_PROJECTED_COLUMN(TableColumnType_CString, TEXT("EventType"), [](const FPackagesTableRow& Row) { return GetLoadTimeProfilerPackageEventTypeString(Row.EventType); })
		UE_TRACE_TABLE_COLUMN(SerializedHeaderSize, TEXT("SerializedHeaderSize"))
		UE_TRACE_TABLE_COLUMN(SerializedExportsCount, TEXT("SerializedExportsCount"))
		UE_TRACE_TABLE_COLUMN(SerializedExportsSize, TEXT("SerializedExportsSize"))
		UE_TRACE_TABLE_COLUMN(MainThreadTime, TEXT("MainThreadTime"))
		UE_TRACE_TABLE_COLUMN(AsyncLoadingThreadTime, TEXT("AsyncLoadingThreadTime"))
	UE_TRACE_TABLE_LAYOUT_END()

	UE_TRACE_TABLE_LAYOUT_BEGIN(FExportsTableLayout, FExportsTableRow)
		UE_TRACE_TABLE_PROJECTED_COLUMN(TableColumnType_CString, TEXT("Package"), [](const FExportsTableRow& Row) { return Row.ExportInfo->Package ? Row.ExportInfo->Package->Name : TEXT("[unknown]"); })
		UE_TRACE_TABLE_PROJECTED_COLUMN(TableColumnType_CString, TEXT("Class"), [](const FExportsTableRow& Row) { return Row.ExportInfo->Class ? Row.ExportInfo->Class->Name : TEXT("[unknown]"); })
		UE_TRACE_TABLE_PROJECTED_COLUMN(TableColumnType_CString, TEXT("EventType"), [](const FExportsTableRow& Row) { return GetLoadTimeProfilerObjectEventTypeString(Row.EventType); })
		UE_TRACE_TABLE_COLUMN(SerializedSize, TEXT("SerializedSize"))
		UE_TRACE_TABLE_COLUMN(MainThreadTime, TEXT("MainThreadTime"))
		UE_TRACE_TABLE_COLUMN(AsyncLoadingThreadTime, TEXT("AsyncLoadingThreadTime"))
	UE_TRACE_TABLE_LAYOUT_END()

	UE_TRACE_TABLE_LAYOUT_BEGIN(FRequestsTableLayout, FLoadRequest)
		UE_TRACE_TABLE_COLUMN(Name, TEXT("Name"))
		UE_TRACE_TABLE_COLUMN(ThreadId, TEXT("ThreadId"))
		UE_TRACE_TABLE_COLUMN(StartTime, TEXT("StartTime"))
		UE_TRACE_TABLE_COLUMN(EndTime, TEXT("EndTime"))
		UE_TRACE_TABLE_PROJECTED_COLUMN(TableColumnType_Int, TEXT("PackageCount"), [](const FLoadRequest& Row) { return Row.Packages.Num(); })
		UE_TRACE_TABLE_PROJECTED_COLUMN(TableColumnType_Int, TEXT("Size"), PackageSizeSum)
		UE_TRACE_TABLE_PROJECTED_COLUMN(TableColumnType_CString, TEXT("FirstPackage"), [](const FLoadRequest& Row) { return Row.Packages.Num() ? Row.Packages[0]->Name : TEXT("N/A"); })
	UE_TRACE_TABLE_LAYOUT_END()

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
	TTableView<FRequestsTableLayout> RequestsTable;
};

}

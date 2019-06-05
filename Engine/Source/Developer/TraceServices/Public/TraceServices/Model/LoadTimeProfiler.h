// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "Serialization/LoadTimeTrace.h"
#include "TraceServices/Containers/Timelines.h"
#include "TraceServices/Containers/Tables.h"
#include "Containers/Array.h"

namespace Trace
{

struct FFileInfo
{
	uint32 Id;
	const TCHAR* Path;
};

enum EFileActivityType
{
	FileActivityType_Open,
	FileActivityType_Close,
	FileActivityType_Read,
	FileActivityType_Write,

	FileActivityType_Count,
};

struct FFileActivity
{
	uint64 Offset;
	uint64 Size;
	EFileActivityType ActivityType;
	bool Failed;
};

class IFileActivityProvider
	: public IProvider
{
public:
	typedef ITimeline<FFileActivity> Timeline;

	virtual ~IFileActivityProvider() = default;
	virtual void EnumerateFileActivity(TFunctionRef<bool(const FFileInfo&, const Timeline&)> Callback) const = 0;
};

struct FPackageSummaryInfo
{
	uint32 TotalHeaderSize = 0;
	uint32 NameCount = 0;
	uint32 ImportCount = 0;
	uint32 ExportCount = 0;
};

struct FClassInfo
{
	const TCHAR* Name;
};

struct FPackageExportInfo
{
	uint32 Id;
	const FClassInfo* Class = nullptr;
	uint64 SerialOffset = 0;
	uint64 SerialSize = 0;
	bool IsAsset = false;
};

struct FPackageInfo
{
	uint32 Id;
	const TCHAR* Name = nullptr;
	FPackageSummaryInfo Summary;
	TArray<FPackageExportInfo*> Exports;
};

struct FLoadTimeProfilerCpuEvent
{
	const FPackageInfo* Package = nullptr;
	const FPackageExportInfo* Export = nullptr;
	ELoadTimeProfilerPackageEventType PackageEventType = LoadTimeProfilerPackageEventType_None;
	ELoadTimeProfilerObjectEventType ExportEventType = LoadTimeProfilerObjectEventType_None;
};

struct FLoadTimeProfilerAggregatedStats
{
	const TCHAR* Name;
	uint64 Count;
	double Total;
	double Min;
	double Max;
	double Average;
	double Median;
};

class ILoadTimeProfilerProvider
	: public IProvider
{
public:
	typedef ITimeline<FLoadTimeProfilerCpuEvent> CpuTimeline;

	virtual ~ILoadTimeProfilerProvider() = default;
	virtual uint64 GetPackageCount() const = 0;
	virtual void EnumeratePackages(TFunctionRef<void(const FPackageInfo&)> Callback) const = 0;
	virtual uint32 GetMainThreadId() const = 0;
	virtual void ReadMainThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const = 0;
	virtual uint32 GetAsyncLoadingThreadId() const = 0;
	virtual void ReadAsyncLoadingThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const = 0;
	virtual ITable<FLoadTimeProfilerAggregatedStats>* CreateEventAggregation(double IntervalStart, double IntervalEnd) const = 0;
	virtual ITable<FLoadTimeProfilerAggregatedStats>* CreateObjectTypeAggregation(double IntervalStart, double IntervalEnd) const = 0;
};

TRACESERVICES_API const ILoadTimeProfilerProvider* ReadLoadTimeProfilerProvider(const IAnalysisSession& Session);
TRACESERVICES_API const IFileActivityProvider* ReadFileActivityProvider(const IAnalysisSession& Session);

}
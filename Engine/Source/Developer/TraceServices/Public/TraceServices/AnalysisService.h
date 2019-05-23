// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Templates/SharedPointer.h"
#include "Trace/Store.h"
#include "Delegates/Delegate.h"
#include "GenericPlatform/GenericPlatformAffinity.h"
#include "Serialization/LoadTimeTrace.h"
#include "Templates/UniquePtr.h"

namespace Trace
{

enum ETableColumnType
{
	TableColumnType_Invalid,
	TableColumnType_Bool,
	TableColumnType_Int,
	TableColumnType_Float,
	TableColumnType_Double,
	TableColumnType_CString,
};

class ITableLayout
{
public:
	virtual ~ITableLayout() = default;
	virtual uint8 GetColumnCount() const = 0;
	virtual const TCHAR* GetColumnName(uint8 ColumnIndex) const = 0;
	virtual ETableColumnType GetColumnType(uint8 ColumnIndex) const = 0;
};

class IUntypedTableReader
{
public:
	virtual ~IUntypedTableReader() = default;
	virtual bool IsValid() const = 0;
	virtual void NextRow() = 0;
	virtual void SetRowIndex(uint64 RowIndex) = 0;
	virtual bool GetValueBool(uint8 ColumnIndex) const = 0;
	virtual int64 GetValueInt(uint8 ColumnIndex) const = 0;
	virtual float GetValueFloat(uint8 ColumnIndex) const = 0;
	virtual double GetValueDouble(uint8 ColumnIndex) const = 0;
	virtual const TCHAR* GetValueCString(uint8 ColumnIndex) const = 0;
};

template<typename RowType>
class ITableReader
	: public IUntypedTableReader
{
public:
	virtual const RowType* GetCurrentRow() const = 0;
};

class IUntypedTable
{
public:
	virtual ~IUntypedTable() = default;
	virtual const ITableLayout& GetLayout() const = 0;
	virtual uint64 GetRowCount() const = 0;
	virtual IUntypedTableReader* CreateReader() const = 0;
};

template<typename RowType>
class ITable
	: public IUntypedTable
{
public:
	virtual ~ITable() = default;
	virtual ITableReader<RowType>* CreateReader() const = 0;
};

struct FBookmark
{
	double Time;
	const TCHAR* Text;
};

class IBookmarkProvider
{
public:
	virtual ~IBookmarkProvider() = default;
	virtual uint64 GetBookmarkCount() const = 0;
	virtual void EnumerateBookmarks(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FBookmark&)> Callback) const = 0;
};

struct FLogCategory
{
	const TCHAR* Name = nullptr;
	ELogVerbosity::Type DefaultVerbosity;
};

struct FLogMessage
{
	uint64 Index;
	double Time;
	const FLogCategory* Category = nullptr;
	const TCHAR* File = nullptr;
	const TCHAR* Message = nullptr;
	int32 Line;
	ELogVerbosity::Type Verbosity;
};

class ILogProvider
{
public:
	virtual ~ILogProvider() = default;
	virtual uint64 GetMessageCount() const = 0;
	virtual void EnumerateMessages(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FLogMessage&)> Callback) const = 0;
	virtual void EnumerateMessagesByIndex(uint64 Start, uint64 End, TFunctionRef<void(const FLogMessage&)> Callback) const = 0;
	virtual bool ReadMessage(uint64 Index, TFunctionRef<void(const FLogMessage&)> Callback) const = 0;
	virtual uint64 GetCategoryCount() const = 0;
	virtual void EnumerateCategories(TFunctionRef<void(const FLogCategory&)> Callback) const = 0;
	virtual const IUntypedTable& GetMessagesTable() const = 0;
};

struct FThreadInfo
{
	uint32 Id;
	const TCHAR* Name;
	const TCHAR* GroupName;
};

class IThreadProvider
{
public:
	virtual ~IThreadProvider() = default;
	virtual uint64 GetModCount() const = 0;
	virtual void EnumerateThreads(TFunctionRef<void(const FThreadInfo&)> Callback) const = 0;
};

template<typename InEventType>
class ITimeline
{
public:
	typedef InEventType EventType;

	virtual ~ITimeline() = default;
	virtual uint64 GetModCount() const = 0;
	virtual uint64 GetEventCount() const = 0;
	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, TFunctionRef<void(bool, double, const EventType&)> Callback) const = 0;
	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, TFunctionRef<void(double, double, uint32, const EventType&)> Callback) const = 0;
	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, TFunctionRef<void(bool, double, const EventType&)> Callback) const = 0;
	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, double, uint32, const EventType&)> Callback) const = 0;
};

struct FTimingProfilerTimer
{
	const TCHAR* Name;
	uint32 Id;
	uint32 NameHash;
	bool IsGpuTimer;
};

struct FTimingProfilerEvent
{
	uint32 TimerIndex;
};

struct FAggregatedTimingStats
{
	uint64 InstanceCount = 0;
	double TotalInclusiveTime = 0.0;
	double MinInclusiveTime = DBL_MAX;
	double MaxInclusiveTime = -DBL_MAX;
	double AverageInclusiveTime = 0.0;
	double MedianInclusiveTime = 0.0;
	double TotalExclusiveTime = 0.0;
	double MinExclusiveTime = DBL_MAX;
	double MaxExclusiveTime = -DBL_MAX;
	double AverageExclusiveTime = 0.0;
	double MedianExclusiveTime = 0.0;
};

struct FTimingProfilerAggregatedStats
	: public FAggregatedTimingStats
{
	const FTimingProfilerTimer* Timer = nullptr;
};

class ITimingProfilerProvider
{
public:
	typedef ITimeline<FTimingProfilerEvent> Timeline;

	virtual ~ITimingProfilerProvider() = default;
	virtual bool GetCpuThreadTimelineIndex(uint32 ThreadId, uint32& OutTimelineIndex) const = 0;
	virtual bool GetGpuTimelineIndex(uint32& OutTimelineIndex) const = 0;
	virtual bool ReadTimeline(uint32 Index, TFunctionRef<void(const Timeline&)> Callback) const = 0;
	virtual uint64 GetTimelineCount() const = 0;
	virtual void EnumerateTimelines(TFunctionRef<void(const Timeline&)> Callback) const = 0;
	virtual void ReadTimers(TFunctionRef<void(const FTimingProfilerTimer*, uint64)> Callback) const = 0;
	virtual ITable<FTimingProfilerAggregatedStats>* CreateAggregation(double IntervalStart, double IntervalEnd, TFunctionRef<bool(uint32)> CpuThreadFilter, bool IncludeGpu) const = 0;
};

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
{
public:
	typedef ITimeline<FFileActivity> Timeline;

	virtual ~IFileActivityProvider() = default;
	virtual void EnumerateFileActivity(TFunctionRef<bool(const FFileInfo&, const Timeline&)> Callback) const = 0;
};

struct FFrame
{
	uint64 Index;
	double StartTime;
	double EndTime;
};

class IFrameProvider
{
public:
	virtual ~IFrameProvider() = default;
	virtual uint64 GetFrameCount(ETraceFrameType FrameType) const = 0;
	virtual void EnumerateFrames(ETraceFrameType FrameType, uint64 Start, uint64 End, TFunctionRef<void(const FFrame&)> Callback) const = 0;
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
{
public:
	typedef ITimeline<FLoadTimeProfilerCpuEvent> CpuTimeline;

	virtual ~ILoadTimeProfilerProvider() = default;
	virtual uint64 GetPackageCount() const = 0;
	virtual void EnumeratePackages(TFunctionRef<void(const FPackageInfo&)> Callback) const = 0; 
	virtual void ReadMainThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const = 0;
	virtual void ReadAsyncLoadingThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const = 0;
	virtual ITable<FLoadTimeProfilerAggregatedStats>* CreateEventAggregation(double IntervalStart, double IntervalEnd) const = 0;
	virtual ITable<FLoadTimeProfilerAggregatedStats>* CreateObjectTypeAggregation(double IntervalStart, double IntervalEnd) const = 0;
};

enum ECounterDisplayHint
{
	CounterDisplayHint_None,
	CounterDisplayHint_Memory,
	CounterDisplayHint_FloatingPoint,
};

class ICounter
{
public:
	virtual ~ICounter() = default;

	virtual const TCHAR* GetName() const = 0;
	virtual uint32 GetId() const = 0;
	virtual ECounterDisplayHint GetDisplayHint() const = 0;
	virtual void EnumerateValues(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, int64)> Callback) const = 0;
	virtual void EnumerateFloatValues(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, double)> Callback) const = 0;
};

class ICounterProvider
{
public:
	virtual ~ICounterProvider() = default;
	virtual uint64 GetCounterCount() const = 0;
	virtual void EnumerateCounters(TFunctionRef<void(const ICounter&)> Callback) const = 0;
};

class IAnalysisSession
{
public:
	virtual ~IAnalysisSession() = default;
	
	virtual const TCHAR* GetName() const = 0;
	virtual bool IsAnalysisComplete() const = 0;
	virtual double GetDurationSeconds() const = 0;

	virtual void BeginRead() const = 0;
	virtual void EndRead() const = 0;
	virtual const IBookmarkProvider& ReadBookmarkProvider() const = 0;
	virtual const ILogProvider& ReadLogProvider() const = 0;
	virtual const IThreadProvider& ReadThreadProvider() const = 0;
	virtual const IFrameProvider& ReadFrameProvider() const = 0;
	virtual const ITimingProfilerProvider& ReadTimingProfilerProvider() const = 0;
	virtual const IFileActivityProvider& ReadFileActivityProvider() const = 0;
	virtual const ILoadTimeProfilerProvider& ReadLoadTimeProfilerProvider() const = 0;
	virtual const ICounterProvider& ReadCounterProvider() const = 0;
};

struct FAnalysisSessionReadScope
{
	FAnalysisSessionReadScope(const IAnalysisSession& InAnalysisSession)
		: AnalysisSession(InAnalysisSession)
	{
		AnalysisSession.BeginRead();
	}

	~FAnalysisSessionReadScope()
	{
		AnalysisSession.EndRead();
	}

private:
	const IAnalysisSession& AnalysisSession;
};

class IAnalysisService
{
public:
	virtual TSharedPtr<const IAnalysisSession> Analyze(const TCHAR* SessionName, TUniquePtr<Trace::IInDataStream>&& DataStream) = 0;
	virtual TSharedPtr<const IAnalysisSession> StartAnalysis(const TCHAR* SessionName, TUniquePtr<Trace::IInDataStream>&& DataStream) = 0;

	DECLARE_EVENT_OneParam(IAnalysisService, FAnalysisStartedEvent, TSharedRef<const IAnalysisSession>)
	virtual FAnalysisStartedEvent& OnAnalysisStarted() = 0;

	DECLARE_EVENT_OneParam(IAnalysisService, FAnalysisFinishedEvent, TSharedRef<const IAnalysisSession>)
	virtual FAnalysisFinishedEvent& OnAnalysisFinished() = 0;
};

}

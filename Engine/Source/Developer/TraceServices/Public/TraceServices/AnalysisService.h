// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"
#include "Trace/Store.h"
#include "Delegates/Delegate.h"
#include "GenericPlatform/GenericPlatformAffinity.h"
#include "Serialization/LoadTimeTrace.h"

namespace Trace
{

struct FBookmark
{
	double Time;
	const TCHAR* Text;
};

class IBookmarkProvider
{
public:
	virtual ~IBookmarkProvider() = default;
	virtual void EnumerateBookmarks(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FBookmark&)> Callback) const = 0;
};

struct FLogCategory
{
	FString Name;
	ELogVerbosity::Type DefaultVerbosity;
};

struct FLogMessage
{
	uint64 Index;
	double Time;
	const FLogCategory* Category;
	const TCHAR* File;
	const TCHAR* Message;
	int32 Line;
	ELogVerbosity::Type Verbosity;
};

class ILogProvider
{
public:
	virtual ~ILogProvider() = default;
	virtual uint64 GetMessageCount() const = 0;
	virtual void EnumerateMessages(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FLogMessage&)> Callback, bool ResolveFormatString = true) const = 0;
	virtual void EnumerateMessagesByIndex(uint64 Start, uint64 End, TFunctionRef<void(const FLogMessage&)> Callback, bool ResolveFormatString = true) const = 0;
	virtual bool ReadMessage(uint64 Index, TFunctionRef<void(const FLogMessage&)> Callback, bool ResolveFormatString = true) const = 0;
	virtual void EnumerateCategories(TFunctionRef<void(const FLogCategory&)> Callback) const = 0;
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
	virtual void EnumerateThreads(TFunctionRef<void(const FThreadInfo&)> Callback) const = 0;
};

template<typename EventType>
class ITimeline
{
public:
	virtual ~ITimeline() = default;
	virtual uint64 GetEventCount() const = 0;
	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, TFunctionRef<void(bool, double, const EventType&)> Callback) const = 0;
	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, TFunctionRef<void(double, double, uint32, const EventType&)> Callback) const = 0;
	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, TFunctionRef<void(bool, double, const EventType&)> Callback) const = 0;
	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, double, uint32, const EventType&)> Callback) const = 0;
};

struct FTimingProfilerTimer
{
	FString Name;
	uint32 Id;
	uint32 NameHash;
	bool IsGpuTimer;
};

struct FTimingProfilerEvent
{
	uint32 TimerIndex;
};

class ITimingProfilerProvider
{
public:
	typedef ITimeline<FTimingProfilerEvent> Timeline;

	virtual ~ITimingProfilerProvider() = default;
	virtual bool GetCpuThreadTimelineIndex(uint32 ThreadId, uint32& OutTimelineIndex) const = 0;
	virtual bool GetGpuTimelineIndex(uint32& OutTimelineIndex) const = 0;
	virtual bool ReadTimeline(uint32 Index, TFunctionRef<void(const Timeline&)> Callback) const = 0;
	virtual void EnumerateTimelines(TFunctionRef<void(const Timeline&)> Callback) const = 0;
	virtual void ReadTimers(TFunctionRef<void(const FTimingProfilerTimer*, uint64)> Callback) const = 0;
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
	FString Name;
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
	FString Name;
	FPackageSummaryInfo Summary;
	TArray<FPackageExportInfo*> Exports;
};

struct FLoadTimeProfilerCpuEvent
{
	const FPackageInfo* Package = nullptr;
	const FPackageExportInfo* Export = nullptr;
	ELoadTimeProfilePackageEventType PackageEventType = LoadTimeProfilerPackageEventType_None;
	ELoadTimeProfilerObjectEventType ExportEventType = LoadTimeProfilerObjectEventType_None;
};

class ILoadTimeProfilerProvider
{
public:
	typedef ITimeline<FLoadTimeProfilerCpuEvent> CpuTimeline;

	virtual ~ILoadTimeProfilerProvider() = default;
	virtual void EnumeratePackages(TFunctionRef<void(const FPackageInfo&)> Callback) const = 0; 
	virtual void ReadMainThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const = 0;
	virtual void ReadAsyncLoadingThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const = 0;
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

class ICountersProvider
{
public:
	virtual ~ICountersProvider() = default;
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
	virtual void ReadBookmarkProvider(TFunctionRef<void(const IBookmarkProvider&)> Callback) const = 0;
	virtual void ReadLogProvider(TFunctionRef<void(const ILogProvider&)> Callback) const = 0;
	virtual void ReadThreadProvider(TFunctionRef<void(const IThreadProvider&)> Callback) const = 0;
	virtual void ReadFramesProvider(TFunctionRef<void(const IFrameProvider&)> Callback) const = 0;
	virtual void ReadTimingProfilerProvider(TFunctionRef<void(const ITimingProfilerProvider&)> Callback) const = 0;
	virtual void ReadFileActivityProvider(TFunctionRef<void(const IFileActivityProvider&)> Callback) const = 0;
	virtual void ReadLoadTimeProfilerProvider(TFunctionRef<void(const ILoadTimeProfilerProvider&)> Callback) const = 0;
	virtual void ReadCountersProvider(TFunctionRef<void(const ICountersProvider&)> Callback) const = 0;
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
	virtual TSharedPtr<const IAnalysisSession> MockAnalysis() = 0;

	DECLARE_EVENT_OneParam(IAnalysisService, FAnalysisStartedEvent, TSharedRef<const IAnalysisSession>)
	virtual FAnalysisStartedEvent& OnAnalysisStarted() = 0;

	DECLARE_EVENT_OneParam(IAnalysisService, FAnalysisFinishedEvent, TSharedRef<const IAnalysisSession>)
	virtual FAnalysisFinishedEvent& OnAnalysisFinished() = 0;
};

}
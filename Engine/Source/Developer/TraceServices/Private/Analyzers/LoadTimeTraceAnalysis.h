// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Trace/Analyzer.h"
#include "AnalysisServicePrivate.h"
#include "Model/LoadTimeProfilerPrivate.h"

namespace Trace
{
	struct FClassInfo;
}

inline bool operator!=(const Trace::FLoadTimeProfilerCpuEvent& Lhs, const Trace::FLoadTimeProfilerCpuEvent& Rhs)
{
	return Lhs.Package != Rhs.Package ||
		Lhs.Export != Rhs.Export ||
		Lhs.PackageEventType != Rhs.PackageEventType ||
		Lhs.ExportEventType != Rhs.ExportEventType;
}

class FAsyncLoadingTraceAnalyzer : public Trace::IAnalyzer
{
public:
	FAsyncLoadingTraceAnalyzer(Trace::IAnalysisSession& Session, Trace::FLoadTimeProfilerProvider& LoadTimeProfilerProvider);
	virtual ~FAsyncLoadingTraceAnalyzer();

	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	virtual void OnAnalysisEnd() override;
	
private:
	struct FRequestState;
	struct FAsyncPackageState;

	struct FLoadMapState
	{
		uint64 Id;
		FString Name;
		uint64 WallTimeStartCycle;
		uint64 WallTimeEndCycle;
		TArray<TSharedRef<FRequestState>> Requests;
	};

	struct FStreamableHandleState
	{
		uint64 Id;
		FString DebugName;
		uint64 WallTimeStartCycle;
		uint64 WallTimeEndCycle;
		TArray<TSharedRef<FRequestState>> Requests;
	};

	struct FRequestState
	{
		uint64 Id;
		uint64 WallTimeStartCycle;
		uint64 WallTimeEndCycle;
		TArray<TSharedRef<FAsyncPackageState>> AsyncPackages;
	};

	struct FLinkerState
	{
		TSharedPtr<FAsyncPackageState> AsyncPackageState;
		Trace::FPackageInfo* PackageInfo = nullptr;
	};

	struct FAsyncPackageState
	{
		Trace::FPackageInfo* PackageInfo = nullptr;
		TSharedPtr<FLinkerState> LinkerState;
		TArray<TSharedRef<FRequestState>> Requests;
	};

	struct FScopeStackEntry
	{
		Trace::FLoadTimeProfilerCpuEvent Event;
		bool EnteredEvent;
	};

	struct FThreadState
	{
		FScopeStackEntry CpuScopeStack[256];
		uint64 CpuScopeStackDepth = 0;
		Trace::FLoadTimeProfilerCpuEvent CurrentEvent;
		uint64 WaitForStreamableHandleStartCycle = 0;
		TSharedPtr<FStreamableHandleState> WaitForStreamableHandleHandle;
		TSharedPtr<FLoadMapState> ActiveLoadMap;
		
		TSharedPtr<Trace::FLoadTimeProfilerProvider::CpuTimelineInternal> CpuTimeline;

		void EnterPackageScope(double Time, const Trace::FPackageInfo* PackageInfo, ELoadTimeProfilerPackageEventType EventType)
		{
			FScopeStackEntry& StackEntry = CpuScopeStack[CpuScopeStackDepth++];
			StackEntry.Event.Export = CurrentEvent.Export;
			StackEntry.Event.ExportEventType = CurrentEvent.ExportEventType;
			StackEntry.Event.Package = PackageInfo;
			StackEntry.Event.PackageEventType = EventType;
			if (PackageInfo && CurrentEvent != StackEntry.Event)
			{
				CurrentEvent = StackEntry.Event;
				CpuTimeline->AppendBeginEvent(Time, StackEntry.Event);
				StackEntry.EnteredEvent = true;
			}
			else
			{
				StackEntry.EnteredEvent = false;
			}
		}

		void EnterExportScope(double Time, const Trace::FPackageExportInfo* ExportInfo, ELoadTimeProfilerObjectEventType EventType)
		{
			FScopeStackEntry& StackEntry = CpuScopeStack[CpuScopeStackDepth++];
			StackEntry.Event.Export = ExportInfo;
			StackEntry.Event.ExportEventType = EventType;
			StackEntry.Event.Package = CurrentEvent.Package;
			StackEntry.Event.PackageEventType = CurrentEvent.PackageEventType;

			if (ExportInfo && CurrentEvent != StackEntry.Event)
			{
				CurrentEvent = StackEntry.Event;
				CpuTimeline->AppendBeginEvent(Time, StackEntry.Event);
				StackEntry.EnteredEvent = true;
			}
			else
			{
				StackEntry.EnteredEvent = false;
			}
		}

		void LeaveScope(double Time)
		{
			FScopeStackEntry& StackEntry = CpuScopeStack[--CpuScopeStackDepth];
			if (StackEntry.EnteredEvent)
			{
				CpuTimeline->AppendEndEvent(Time);
			}
			if (CpuScopeStackDepth > 0)
			{
				CurrentEvent = CpuScopeStack[CpuScopeStackDepth - 1].Event;
			}
			else
			{
				CurrentEvent = Trace::FLoadTimeProfilerCpuEvent();
			}
		}

		Trace::FPackageExportInfo* GetCurrentExportScope()
		{
			if (CpuScopeStackDepth > 0)
			{
				FScopeStackEntry& StackEntry = CpuScopeStack[CpuScopeStackDepth - 1];
				return const_cast<Trace::FPackageExportInfo*>(StackEntry.Event.Export);
			}
			else
			{
				return nullptr;
			}
		}
	};

	TSharedRef<FThreadState> GetThreadState(uint32 ThreadId);
	const Trace::FClassInfo* GetClassInfo(uint64 ClassPtr) const;

	enum : uint16
	{
		RouteId_StartAsyncLoading,
		RouteId_SuspendAsyncLoading,
		RouteId_ResumeAsyncLoading,
		RouteId_NewLinker,
		RouteId_DestroyLinker,
		RouteId_NewAsyncPackage,
		RouteId_DestroyAsyncPackage,
		RouteId_BeginRequest,
		RouteId_EndRequest,
		RouteId_BeginLoadMap,
		RouteId_EndLoadMap,
		RouteId_NewStreamableHandle,
		RouteId_DestroyStreamableHandle,
		RouteId_BeginLoadStreamableHandle,
		RouteId_EndLoadStreamableHandle,
		RouteId_BeginWaitForStreamableHandle,
		RouteId_EndWaitForStreamableHandle,
		RouteId_PackageSummary,
		RouteId_StreamableHandleRequestAssociation,
		RouteId_AsyncPackageRequestAssociation,
		RouteId_AsyncPackageLinkerAssociation,
		RouteId_LinkerArchiveAssociation,
		RouteId_BeginAsyncPackageScope,
		RouteId_EndAsyncPackageScope,
		RouteId_BeginCreateExport,
		RouteId_EndCreateExport,
		RouteId_BeginObjectScope,
		RouteId_EndObjectScope,
		RouteId_BeginFlushAsyncLoading,
		RouteId_EndFlushAsyncLoading,
		RouteId_ClassInfo,
	};

	Trace::IAnalysisSession& Session;
	Trace::FLoadTimeProfilerProvider& LoadTimeProfilerProvider;
	//TSharedPtr<Trace::FMonotonicTimeline<uint64>> BlockingRequestsTimeline;

	TArray<TSharedRef<FLoadMapState>> Maps;
	TArray<TSharedRef<FStreamableHandleState>> StreamableHandles;
	TArray<TSharedRef<FRequestState>> Requests;

	TMap<uint64, TSharedRef<FLinkerState>> ActiveLinkersMap;
	TMap<uint64, TSharedRef<FAsyncPackageState>> ActiveAsyncPackagesMap;
	TMap<uint64, Trace::FPackageExportInfo*> ExportsMap;
	TMap<uint64, TSharedRef<FStreamableHandleState>> ActiveStreamableHandlesMap;
	TMap<uint64, TSharedRef<FRequestState>> ActiveRequestsMap;
	TMap<uint32, TSharedRef<FThreadState>> ThreadStatesMap;
	TMap<uint64, const Trace::FClassInfo*> ClassInfosMap;

	uint64 FlushAsyncLoadingRequestId;
	uint64 FlushAsyncLoadingStartCycle;

	uint64 NextMapId = 0;
	uint64 NextStreamableHandleId = 0;
	int64 MainThreadId = -1;
	int64 AsyncLoadingThreadId = -1;
};

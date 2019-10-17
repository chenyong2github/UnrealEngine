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
	virtual bool OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	
private:
	struct FRequestState;
	struct FAsyncPackageState;

	struct FRequestGroupState
	{
		FString Name;
		TArray<TSharedRef<FRequestState>> Requests;
		Trace::FLoadRequest* LoadRequest = nullptr;
		uint64 LatestEndCycle = 0;
		uint64 ActiveRequestsCount = 0;
		bool bIsClosed = false;
	};

	struct FRequestState
	{
		uint64 WallTimeStartCycle;
		uint64 WallTimeEndCycle;
		uint32 ThreadId;
		TSharedPtr<FRequestGroupState> Group;
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
		TSharedPtr<FRequestState> Request;
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
		TArray<TSharedPtr<FRequestGroupState>> RequestGroupStack;
		
		Trace::FLoadTimeProfilerProvider::CpuTimelineInternal* CpuTimeline;

		void EnterPackageScope(double Time, const Trace::FPackageInfo* PackageInfo, ELoadTimeProfilerPackageEventType EventType);
		void EnterExportScope(double Time, const Trace::FPackageExportInfo* ExportInfo, ELoadTimeProfilerObjectEventType EventType);
		void LeaveScope(double Time);
		Trace::FPackageExportInfo* GetCurrentExportScope();
	};

	void PackageRequestAssociation(const FOnEventContext& Context, TSharedRef<FAsyncPackageState> AsyncPackageState, TSharedRef<FRequestState> RequestState);
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
		RouteId_BeginRequestGroup,
		RouteId_EndRequestGroup,
		RouteId_PackageSummary,
		RouteId_AsyncPackageRequestAssociation,
		RouteId_AsyncPackageLinkerAssociation,
		RouteId_AsyncPackageImportDependency,
		RouteId_BeginAsyncPackageScope,
		RouteId_EndAsyncPackageScope,
		RouteId_BeginCreateExport,
		RouteId_EndCreateExport,
		RouteId_BeginObjectScope,
		RouteId_EndObjectScope,
		RouteId_ClassInfo,
	};

	enum
	{
		FormatBufferSize = 65536
	};
	TCHAR FormatBuffer[FormatBufferSize];
	TCHAR TempBuffer[FormatBufferSize];

	Trace::IAnalysisSession& Session;
	Trace::FLoadTimeProfilerProvider& LoadTimeProfilerProvider;

	//TArray<TSharedRef<FRequestState>> Requests;

	TMap<uint64, TSharedRef<FLinkerState>> ActiveLinkersMap;
	TMap<uint64, TSharedRef<FAsyncPackageState>> ActiveAsyncPackagesMap;
	TMap<uint64, Trace::FPackageExportInfo*> ExportsMap;
	TMap<uint64, TSharedRef<FRequestState>> ActiveRequestsMap;
	TMap<uint32, TSharedRef<FThreadState>> ThreadStatesMap;
	TMap<uint64, const Trace::FClassInfo*> ClassInfosMap;

	int64 MainThreadId = -1;
	int64 AsyncLoadingThreadId = -1;
};

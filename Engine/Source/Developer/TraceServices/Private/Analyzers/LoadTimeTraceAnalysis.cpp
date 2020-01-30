// Copyright Epic Games, Inc. All Rights Reserved.
#include "LoadTimeTraceAnalysis.h"

#include "Serialization/LoadTimeTrace.h"
#include "Model/LoadTimeProfilerPrivate.h"
#include "Analyzers/MiscTraceAnalysis.h"
#include "Common/FormatArgs.h"
#include <limits>

FAsyncLoadingTraceAnalyzer::FAsyncLoadingTraceAnalyzer(Trace::IAnalysisSession& InSession, Trace::FLoadTimeProfilerProvider& InLoadTimeProfilerProvider)
	: Session(InSession)
	, LoadTimeProfilerProvider(InLoadTimeProfilerProvider)
{
}

FAsyncLoadingTraceAnalyzer::~FAsyncLoadingTraceAnalyzer()
{
	for (const auto& KV : ThreadStatesMap)
	{
		delete KV.Value;
	}
	for (const auto& KV : ActiveAsyncPackagesMap)
	{
		delete KV.Value;
	}
	for (const auto& KV : ActiveRequestsMap)
	{
		delete KV.Value;
	}
}

FAsyncLoadingTraceAnalyzer::FThreadState& FAsyncLoadingTraceAnalyzer::GetThreadState(uint32 ThreadId)
{
	FThreadState* ThreadState = ThreadStatesMap.FindRef(ThreadId);
	if (!ThreadState)
	{
		ThreadState = new FThreadState();
		ThreadStatesMap.Add(ThreadId, ThreadState);
		ThreadState->CpuTimeline = &LoadTimeProfilerProvider.EditCpuTimeline(ThreadId);
	}
	return *ThreadState;
}

const Trace::FClassInfo* FAsyncLoadingTraceAnalyzer::GetClassInfo(uint64 ClassPtr) const
{
	const Trace::FClassInfo* const* ClassInfo = ClassInfosMap.Find(ClassPtr);
	if (ClassInfo)
	{
		return *ClassInfo;
	}
	else
	{
		return nullptr;
	}
}

void FAsyncLoadingTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_StartAsyncLoading, "LoadTime", "StartAsyncLoading");
	Builder.RouteEvent(RouteId_SuspendAsyncLoading, "LoadTime", "SuspendAsyncLoading");
	Builder.RouteEvent(RouteId_ResumeAsyncLoading, "LoadTime", "ResumeAsyncLoading");
	Builder.RouteEvent(RouteId_PackageSummary, "LoadTime", "PackageSummary");
	Builder.RouteEvent(RouteId_BeginCreateExport, "LoadTime", "BeginCreateExport");
	Builder.RouteEvent(RouteId_EndCreateExport, "LoadTime", "EndCreateExport");
	Builder.RouteEvent(RouteId_BeginSerializeExport, "LoadTime", "BeginSerializeExport");
	Builder.RouteEvent(RouteId_EndSerializeExport, "LoadTime", "EndSerializeExport");
	Builder.RouteEvent(RouteId_BeginPostLoadExport, "LoadTime", "BeginPostLoadExport");
	Builder.RouteEvent(RouteId_EndPostLoadExport, "LoadTime", "EndPostLoadExport");
	Builder.RouteEvent(RouteId_NewAsyncPackage, "LoadTime", "NewAsyncPackage");
	Builder.RouteEvent(RouteId_BeginLoadAsyncPackage, "LoadTime", "BeginLoadAsyncPackage");
	Builder.RouteEvent(RouteId_EndLoadAsyncPackage, "LoadTime", "EndLoadAsyncPackage");
	Builder.RouteEvent(RouteId_DestroyAsyncPackage, "LoadTime", "DestroyAsyncPackage");
	Builder.RouteEvent(RouteId_BeginRequest, "LoadTime", "BeginRequest");
	Builder.RouteEvent(RouteId_EndRequest, "LoadTime", "EndRequest");
	Builder.RouteEvent(RouteId_BeginRequestGroup, "LoadTime", "BeginRequestGroup");
	Builder.RouteEvent(RouteId_EndRequestGroup, "LoadTime", "EndRequestGroup");
	Builder.RouteEvent(RouteId_AsyncPackageRequestAssociation, "LoadTime", "AsyncPackageRequestAssociation");
	Builder.RouteEvent(RouteId_AsyncPackageImportDependency, "LoadTime", "AsyncPackageImportDependency");
	Builder.RouteEvent(RouteId_ClassInfo, "LoadTime", "ClassInfo");
	Builder.RouteEvent(RouteId_BatchIssued, "IoDispatcher", "BatchIssued");
	Builder.RouteEvent(RouteId_BatchResolved, "IoDispatcher", "BatchResolved");
}

bool FAsyncLoadingTraceAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_PackageSummary:
	{
		uint64 AsyncPackagePtr = EventData.GetValue<uint64>("AsyncPackage");
		FAsyncPackageState* AsyncPackage = ActiveAsyncPackagesMap.FindRef(AsyncPackagePtr);
		if (AsyncPackage)
		{
			Trace::FAnalysisSessionEditScope _(Session);
			Trace::FPackageSummaryInfo& Summary = AsyncPackage->PackageInfo->Summary;
			Summary.TotalHeaderSize = EventData.GetValue<uint32>("TotalHeaderSize");
			Summary.ImportCount = EventData.GetValue<uint32>("ImportCount");
			Summary.ExportCount = EventData.GetValue<uint32>("ExportCount");
		}
		break;
	}
	case RouteId_BeginCreateExport:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		Trace::FPackageExportInfo& Export = LoadTimeProfilerProvider.CreateExport();
		uint64 AsyncPackagePtr = EventData.GetValue<uint64>("AsyncPackage");
		FAsyncPackageState* AsyncPackage = ActiveAsyncPackagesMap.FindRef(AsyncPackagePtr);
		if (AsyncPackage)
		{
			AsyncPackage->PackageInfo->Exports.Add(&Export);
			Export.Package = AsyncPackage->PackageInfo;
		}
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		FThreadState& ThreadState = GetThreadState(ThreadId);
		ThreadState.EnterExportScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")), &Export, Trace::LoadTimeProfilerObjectEventType_Create);
		break;
	}
	case RouteId_EndCreateExport:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		FThreadState& ThreadState = GetThreadState(ThreadId);
		uint64 ObjectPtr = EventData.GetValue<uint64>("Object");
		Trace::FPackageExportInfo* Export = ThreadState.GetCurrentExportScope();
		if (Export)
		{
			ExportsMap.Add(ObjectPtr, Export);
			const Trace::FClassInfo* ObjectClass = GetClassInfo(EventData.GetValue<uint64>("Class"));
			Export->Class = ObjectClass;
		}
		ThreadState.LeaveExportScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")));
		break;
	}
	case RouteId_BeginSerializeExport:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint64 ObjectPtr = EventData.GetValue<uint64>("Object");
		uint64 SerialSize = EventData.GetValue<uint64>("SerialSize");
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");

		Trace::FPackageExportInfo* Export = ExportsMap.FindRef(ObjectPtr);
		check(Export);

		if (Export)
		{
			Export->SerialSize = SerialSize;
			if (Export->Package)
			{
				const_cast<Trace::FPackageInfo*>(Export->Package)->TotalExportsSerialSize += SerialSize;
			}
		}
		FThreadState& ThreadState = GetThreadState(ThreadId);
		ThreadState.EnterExportScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")), Export, Trace::LoadTimeProfilerObjectEventType_Serialize);
		break;
	}
	case RouteId_EndSerializeExport:
	{
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		FThreadState& ThreadState = GetThreadState(ThreadId);
		{
			Trace::FAnalysisSessionEditScope _(Session);
			ThreadState.LeaveExportScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")));
		}
		break;
	}
	case RouteId_BeginPostLoadExport:
	{
		uint64 ObjectPtr = EventData.GetValue<uint64>("Object");
		Trace::FPackageExportInfo* Export = ExportsMap.FindRef(ObjectPtr);
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		FThreadState& ThreadState = GetThreadState(ThreadId);
		{
			Trace::FAnalysisSessionEditScope _(Session);
			ThreadState.EnterExportScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")), Export, Trace::LoadTimeProfilerObjectEventType_PostLoad);
		}
		break;
	}
	case RouteId_EndPostLoadExport:
	{
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		FThreadState& ThreadState = GetThreadState(ThreadId);
		{
			Trace::FAnalysisSessionEditScope _(Session);
			ThreadState.LeaveExportScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")));
		}
		break;
	}
	case RouteId_BeginRequest:
	{
		uint64 RequestId = EventData.GetValue<uint64>("RequestId");
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		//check(!ActiveRequestsMap.Contains(RequestId));
		FRequestState* RequestState = new FRequestState();
		RequestState->WallTimeStartCycle = EventData.GetValue<uint64>("Cycle");
		RequestState->WallTimeEndCycle = 0;
		RequestState->ThreadId = ThreadId;
		ActiveRequestsMap.Add(RequestId, RequestState);
		FThreadState& ThreadState = GetThreadState(ThreadId);
		TSharedPtr<FRequestGroupState> RequestGroup = ThreadState.RequestGroupStack.Num() ? ThreadState.RequestGroupStack.Top() : nullptr;
		if (!RequestGroup)
		{
			RequestGroup = MakeShared<FRequestGroupState>();
			RequestGroup->Name = TEXT("[ungrouped]");
			RequestGroup->bIsClosed = true;
		}
		RequestGroup->Requests.Add(RequestState);
		++RequestGroup->ActiveRequestsCount;
		RequestState->Group = RequestGroup;
		break;
	}
	case RouteId_EndRequest:
	{
		uint64 RequestId = EventData.GetValue<uint64>("RequestId");
		FRequestState* RequestState = ActiveRequestsMap.FindRef(RequestId);
		if (RequestState)
		{
			RequestState->WallTimeEndCycle = EventData.GetValue<uint64>("Cycle");
			RequestState->Group->LatestEndCycle = FMath::Max(RequestState->Group->LatestEndCycle, RequestState->WallTimeEndCycle);
			--RequestState->Group->ActiveRequestsCount;
			if (RequestState->Group->LoadRequest && RequestState->Group->bIsClosed && RequestState->Group->ActiveRequestsCount == 0)
			{
				Trace::FAnalysisSessionEditScope _(Session);
				RequestState->Group->LoadRequest->EndTime = Context.SessionContext.TimestampFromCycle(RequestState->Group->LatestEndCycle);
			}
		}
		break;
	}
	case RouteId_BeginRequestGroup:
	{
		TSharedRef<FRequestGroupState> GroupState = MakeShared<FRequestGroupState>();
		const TCHAR* FormatString = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());
		const uint8* FormatArgs = EventData.GetAttachment() + (FCString::Strlen(FormatString) + 1) * sizeof(TCHAR);
		Trace::FFormatArgsHelper::Format(FormatBuffer, FormatBufferSize - 1, TempBuffer, FormatBufferSize - 1, FormatString, FormatArgs);
		GroupState->Name = Session.StoreString(FormatBuffer);
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		FThreadState& ThreadState = GetThreadState(ThreadId);
		ThreadState.RequestGroupStack.Push(GroupState);
		break;
	}
	case RouteId_EndRequestGroup:
	{
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		FThreadState& ThreadState = GetThreadState(ThreadId);
		if (ThreadState.RequestGroupStack.Num())
		{
			TSharedPtr<FRequestGroupState> GroupState = ThreadState.RequestGroupStack.Pop(false);
			GroupState->bIsClosed = true;
			if (GroupState->LoadRequest && GroupState->ActiveRequestsCount == 0)
			{
				GroupState->LoadRequest->EndTime = Context.SessionContext.TimestampFromCycle(GroupState->LatestEndCycle);
			}
		}
		break;
	}
	case RouteId_NewAsyncPackage:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint64 AsyncPackagePtr = EventData.GetValue<uint64>("AsyncPackage");
		//check(!ActivePackagesMap.Contains(AsyncPackagePtr));
		FAsyncPackageState* AsyncPackageState = new FAsyncPackageState();
		AsyncPackageState->PackageInfo = &LoadTimeProfilerProvider.EditPackageInfo(reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
		ActiveAsyncPackagesMap.Add(AsyncPackagePtr, AsyncPackageState);
		break;
	}
	case RouteId_BeginLoadAsyncPackage:
	{
		uint64 AsyncPackagePtr = EventData.GetValue<uint64>("AsyncPackage");
		FAsyncPackageState* AsyncPackage = ActiveAsyncPackagesMap.FindRef(AsyncPackagePtr);
		if (AsyncPackage)
		{
			AsyncPackage->LoadStartCycle = EventData.GetValue<uint64>("Cycle");
			if (AsyncPackage->PackageInfo)
			{
				Trace::FAnalysisSessionEditScope _(Session);
				AsyncPackage->LoadHandle = LoadTimeProfilerProvider.BeginLoadPackage(*AsyncPackage->PackageInfo, Context.SessionContext.TimestampFromCycle(AsyncPackage->LoadStartCycle));
			}
		}
		break;
	}
	case RouteId_EndLoadAsyncPackage:
	{
		uint64 AsyncPackagePtr = EventData.GetValue<uint64>("AsyncPackage");
		FAsyncPackageState* AsyncPackage = ActiveAsyncPackagesMap.FindRef(AsyncPackagePtr);
		if (AsyncPackage)
		{
			AsyncPackage->LoadEndCycle = EventData.GetValue<uint64>("Cycle");
			if (AsyncPackage->PackageInfo && AsyncPackage->LoadHandle != uint64(-1))
			{
				Trace::FAnalysisSessionEditScope _(Session);
				LoadTimeProfilerProvider.EndLoadPackage(AsyncPackage->LoadHandle, Context.SessionContext.TimestampFromCycle(AsyncPackage->LoadEndCycle));
			}
		}
		break;
	}
	case RouteId_DestroyAsyncPackage:
	{
		uint64 AsyncPackagePtr = EventData.GetValue<uint64>("AsyncPackage");
		//check(ActiveAsyncPackagesMap.Contains(AsyncPackagePtr));
		ActiveAsyncPackagesMap.Remove(AsyncPackagePtr);
		break;
	}
	case RouteId_AsyncPackageImportDependency:
	{
		uint64 AsyncPackagePtr = EventData.GetValue<uint64>("AsyncPackage");
		FAsyncPackageState* AsyncPackage = ActiveAsyncPackagesMap.FindRef(AsyncPackagePtr);
		uint64 ImportedAsyncPackagePtr = EventData.GetValue<uint64>("ImportedAsyncPackage");
		FAsyncPackageState* ImportedAsyncPackage = ActiveAsyncPackagesMap.FindRef(ImportedAsyncPackagePtr);
		if (AsyncPackage && ImportedAsyncPackage)
		{
			check(AsyncPackage->Request);
			PackageRequestAssociation(Context, ImportedAsyncPackage, AsyncPackage->Request);
		}
		break;
	}
	case RouteId_AsyncPackageRequestAssociation:
	{
		uint64 AsyncPackagePtr = EventData.GetValue<uint64>("AsyncPackage");
		FAsyncPackageState* AsyncPackageState = ActiveAsyncPackagesMap.FindRef(AsyncPackagePtr);
		uint64 RequestId = EventData.GetValue<uint64>("RequestId");
		FRequestState* RequestState = ActiveRequestsMap.FindRef(RequestId);
		if (AsyncPackageState && RequestState)
		{
			PackageRequestAssociation(Context, AsyncPackageState, RequestState);
		}
		break;
	}
	case RouteId_ClassInfo:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint64 ClassPtr = EventData.GetValue<uint64>("Class");
		const Trace::FClassInfo& ClassInfo = LoadTimeProfilerProvider.AddClassInfo(reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
		ClassInfosMap.Add(ClassPtr, &ClassInfo);
		break;
	}
	case RouteId_BatchIssued:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 BatchId = EventData.GetValue<uint64>("BatchId");

		uint64 BatchHandle = LoadTimeProfilerProvider.BeginIoDispatcherBatch(BatchId, Context.SessionContext.TimestampFromCycle(Cycle));
		ActiveBatchesMap.Add(BatchId, BatchHandle);

		break;
	}
	case RouteId_BatchResolved:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 BatchId = EventData.GetValue<uint64>("BatchId");
		uint64 TotalSize = EventData.GetValue<uint64>("TotalSize");

		uint64* FindBatchHandle = ActiveBatchesMap.Find(BatchId);
		if (FindBatchHandle)
		{
			Trace::FAnalysisSessionEditScope _(Session);
			LoadTimeProfilerProvider.EndIoDispatcherBatch(*FindBatchHandle, Context.SessionContext.TimestampFromCycle(Cycle), TotalSize);
		}

		break;
	}
	}

	return true;
}

void FAsyncLoadingTraceAnalyzer::FThreadState::EnterExportScope(double Time, const Trace::FPackageExportInfo* ExportInfo, Trace::ELoadTimeProfilerObjectEventType EventType)
{
	FScopeStackEntry& StackEntry = CpuScopeStack[CpuScopeStackDepth++];
	StackEntry.Event.Export = ExportInfo;
	StackEntry.Event.EventType = EventType;
	StackEntry.Event.Package = ExportInfo ? ExportInfo->Package : nullptr;
	CurrentEvent = StackEntry.Event;
	CpuTimeline->AppendBeginEvent(Time, StackEntry.Event);
}

void FAsyncLoadingTraceAnalyzer::FThreadState::LeaveExportScope(double Time)
{
	FScopeStackEntry& StackEntry = CpuScopeStack[--CpuScopeStackDepth];
	CpuTimeline->AppendEndEvent(Time);
	if (CpuScopeStackDepth > 0)
	{
		CurrentEvent = CpuScopeStack[CpuScopeStackDepth - 1].Event;
	}
	else
	{
		CurrentEvent = Trace::FLoadTimeProfilerCpuEvent();
	}
}

Trace::FPackageExportInfo* FAsyncLoadingTraceAnalyzer::FThreadState::GetCurrentExportScope()
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

void FAsyncLoadingTraceAnalyzer::PackageRequestAssociation(const FOnEventContext& Context, FAsyncPackageState* AsyncPackageState, FRequestState* RequestState)
{
	if (!AsyncPackageState->Request)
	{
		RequestState->AsyncPackages.Add(AsyncPackageState);
		AsyncPackageState->Request = RequestState;
		Trace::FLoadRequest* LoadRequest = RequestState->Group->LoadRequest;
		Trace::FAnalysisSessionEditScope _(Session);
		if (!LoadRequest)
		{
			LoadRequest = &LoadTimeProfilerProvider.CreateRequest();
			LoadRequest->StartTime = Context.SessionContext.TimestampFromCycle(RequestState->WallTimeStartCycle);
			LoadRequest->EndTime = std::numeric_limits<double>::infinity();
			LoadRequest->Name = Session.StoreString(*RequestState->Group->Name);
			LoadRequest->ThreadId = RequestState->ThreadId;
			RequestState->Group->LoadRequest = LoadRequest;
		}
		LoadRequest->Packages.Add(AsyncPackageState->PackageInfo);
	}
}

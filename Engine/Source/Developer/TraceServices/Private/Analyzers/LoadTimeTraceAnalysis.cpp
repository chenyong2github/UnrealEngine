// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "LoadTimeTraceAnalysis.h"

#include "HAL/FileManager.h"
#include "Serialization/LoadTimeTrace.h"
#include "Trace/Trace.h"
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

}

TSharedRef<FAsyncLoadingTraceAnalyzer::FThreadState> FAsyncLoadingTraceAnalyzer::GetThreadState(uint32 ThreadId)
{
	if (!ThreadStatesMap.Contains(ThreadId))
	{
		TSharedRef<FThreadState> ThreadState = MakeShared<FThreadState>();
		ThreadStatesMap.Add(ThreadId, ThreadState);
		if (MainThreadId == -1)
		{
			MainThreadId = ThreadId;
			LoadTimeProfilerProvider.SetMainThreadId(MainThreadId);
			ThreadState->CpuTimeline = &LoadTimeProfilerProvider.EditMainThreadCpuTimeline();
		}
		else if (ThreadId == AsyncLoadingThreadId)
		{
			ThreadState->CpuTimeline = &LoadTimeProfilerProvider.EditAsyncLoadingThreadCpuTimeline();
		}
		else
		{
			ThreadState->CpuTimeline = &LoadTimeProfilerProvider.EditAdditionalCpuTimeline(ThreadId);
		}
		return ThreadState;
	}
	else
	{
		return ThreadStatesMap[ThreadId];
	}
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
	Builder.RouteEvent(RouteId_NewLinker, "LoadTime", "NewLinker");
	Builder.RouteEvent(RouteId_DestroyLinker, "LoadTime", "DestroyLinker");
	Builder.RouteEvent(RouteId_PackageSummary, "LoadTime", "PackageSummary");
	Builder.RouteEvent(RouteId_BeginCreateExport, "LoadTime", "BeginCreateExport");
	Builder.RouteEvent(RouteId_EndCreateExport, "LoadTime", "EndCreateExport");
	Builder.RouteEvent(RouteId_BeginObjectScope, "LoadTime", "BeginObjectScope");
	Builder.RouteEvent(RouteId_EndObjectScope, "LoadTime", "EndObjectScope");
	Builder.RouteEvent(RouteId_NewAsyncPackage, "LoadTime", "NewAsyncPackage");
	Builder.RouteEvent(RouteId_DestroyAsyncPackage, "LoadTime", "DestroyAsyncPackage");
	Builder.RouteEvent(RouteId_BeginRequest, "LoadTime", "BeginRequest");
	Builder.RouteEvent(RouteId_EndRequest, "LoadTime", "EndRequest");
	Builder.RouteEvent(RouteId_BeginRequestGroup, "LoadTime", "BeginRequestGroup");
	Builder.RouteEvent(RouteId_EndRequestGroup, "LoadTime", "EndRequestGroup");
	Builder.RouteEvent(RouteId_AsyncPackageRequestAssociation, "LoadTime", "AsyncPackageRequestAssociation");
	Builder.RouteEvent(RouteId_AsyncPackageLinkerAssociation, "LoadTime", "AsyncPackageLinkerAssociation");
	Builder.RouteEvent(RouteId_AsyncPackageImportDependency, "LoadTime", "AsyncPackageImportDependency");
	Builder.RouteEvent(RouteId_BeginAsyncPackageScope, "LoadTime", "BeginAsyncPackageScope");
	Builder.RouteEvent(RouteId_EndAsyncPackageScope, "LoadTime", "EndAsyncPackageScope");
	Builder.RouteEvent(RouteId_ClassInfo, "LoadTime", "ClassInfo");
}

bool FAsyncLoadingTraceAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_StartAsyncLoading:
	{
		Trace::FAnalysisSessionEditScope _(Session);
		AsyncLoadingThreadId = EventData.GetValue<uint32>("ThreadId");
		LoadTimeProfilerProvider.SetAsyncLoadingThreadId(AsyncLoadingThreadId);
		break;
	}
	case RouteId_NewLinker:
	{
		uint64 LinkerPtr = EventData.GetValue<uint64>("Linker");
		//check(!ActiveLinkersMap.Contains(LinkerPtr));
		TSharedRef<FLinkerState> LinkerState = MakeShared<FLinkerState>();
		ActiveLinkersMap.Add(LinkerPtr, LinkerState);
		break;
	}
	case RouteId_DestroyLinker:
	{
		uint64 Ptr = EventData.GetValue<uint64>("Linker");
		//check(ActiveLinkersMap.Contains(Ptr));
		ActiveLinkersMap.Remove(Ptr);
		break;
	}
	case RouteId_PackageSummary:
	{
		uint64 LinkerPtr = EventData.GetValue<uint64>("Linker");
		TSharedRef<FLinkerState>* LinkerState = ActiveLinkersMap.Find(LinkerPtr);
		if (LinkerState && (*LinkerState)->PackageInfo)
		{
			Trace::FAnalysisSessionEditScope _(Session);
			Trace::FPackageSummaryInfo& Summary = (*LinkerState)->PackageInfo->Summary;
			Summary.TotalHeaderSize = EventData.GetValue<uint32>("TotalHeaderSize");
			Summary.NameCount = EventData.GetValue<uint32>("NameCount");
			Summary.ImportCount = EventData.GetValue<uint32>("ImportCount");
			Summary.ExportCount = EventData.GetValue<uint32>("ExportCount");
		}
		break;
	}
	case RouteId_BeginAsyncPackageScope:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint64 AsyncPackagePtr = EventData.GetValue<uint64>("AsyncPackage");
		TSharedRef<FAsyncPackageState>* AsyncPackageState = ActiveAsyncPackagesMap.Find(AsyncPackagePtr);
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		ELoadTimeProfilerPackageEventType EventType = static_cast<ELoadTimeProfilerPackageEventType>(EventData.GetValue<uint8>("EventType"));
		ThreadState->EnterPackageScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")), AsyncPackageState ? (*AsyncPackageState)->PackageInfo : nullptr, EventType);
		break;
	}
	case RouteId_EndAsyncPackageScope:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		ThreadState->LeaveScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")));
		break;
	}
	case RouteId_BeginCreateExport:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		Trace::FPackageExportInfo& Export = LoadTimeProfilerProvider.CreateExport();
		Export.SerialOffset = EventData.GetValue<uint64>("SerialOffset");
		Export.SerialSize = EventData.GetValue<uint64>("SerialSize");
		Export.IsAsset = EventData.GetValue<bool>("IsAsset");
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		ThreadState->EnterExportScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")), &Export, LoadTimeProfilerObjectEventType_Create);
		uint64 LinkerPtr = EventData.GetValue<uint64>("Linker");
		TSharedRef<FLinkerState>* LinkerState = ActiveLinkersMap.Find(LinkerPtr);
		if (LinkerState && (*LinkerState)->PackageInfo)
		{
			(*LinkerState)->PackageInfo->Exports.Add(&Export);
			Export.Package = (*LinkerState)->PackageInfo;
		}
		break;
	}
	case RouteId_EndCreateExport:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		uint64 ObjectPtr = EventData.GetValue<uint64>("Object");
		Trace::FPackageExportInfo* Export = ThreadState->GetCurrentExportScope();
		if (Export)
		{
			ExportsMap.Add(ObjectPtr, Export);
			const Trace::FClassInfo* ObjectClass = GetClassInfo(EventData.GetValue<uint64>("Class"));
			Export->Class = ObjectClass;
		}
		ThreadState->LeaveScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")));
		break;
	}
	case RouteId_BeginObjectScope:
	{
		uint64 ObjectPtr = EventData.GetValue<uint64>("Object");
		Trace::FPackageExportInfo** Export = ExportsMap.Find(ObjectPtr);
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		ELoadTimeProfilerObjectEventType EventType = static_cast<ELoadTimeProfilerObjectEventType>(EventData.GetValue<uint8>("EventType"));
		{
			Trace::FAnalysisSessionEditScope _(Session);
			ThreadState->EnterExportScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")), Export ? *Export : nullptr, EventType);
		}
		break;
	}
	case RouteId_EndObjectScope:
	{
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		{
			Trace::FAnalysisSessionEditScope _(Session);
			ThreadState->LeaveScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")));
		}
		break;
	}
	case RouteId_BeginRequest:
	{
		uint64 RequestId = EventData.GetValue<uint64>("RequestId");
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		//check(!ActiveRequestsMap.Contains(RequestId));
		TSharedRef<FRequestState> RequestState = MakeShared<FRequestState>();
		RequestState->WallTimeStartCycle = EventData.GetValue<uint64>("Cycle");
		RequestState->WallTimeEndCycle = 0;
		RequestState->ThreadId = ThreadId;
		ActiveRequestsMap.Add(RequestId, RequestState);
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		TSharedPtr<FRequestGroupState> RequestGroup = ThreadState->RequestGroupStack.Num() ? ThreadState->RequestGroupStack.Top() : nullptr;
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
		TSharedRef<FRequestState>* FindRequestState = ActiveRequestsMap.Find(RequestId);
		if (FindRequestState)
		{
			TSharedRef<FRequestState> RequestState = *FindRequestState;
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
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		ThreadState->RequestGroupStack.Push(GroupState);
		break;
	}
	case RouteId_EndRequestGroup:
	{
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		if (ThreadState->RequestGroupStack.Num())
		{
			TSharedPtr<FRequestGroupState> GroupState = ThreadState->RequestGroupStack.Pop(false);
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
		TSharedRef<FAsyncPackageState> AsyncPackageState = MakeShared<FAsyncPackageState>();
		AsyncPackageState->PackageInfo = &LoadTimeProfilerProvider.CreatePackage(reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
		ActiveAsyncPackagesMap.Add(AsyncPackagePtr, AsyncPackageState);
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
		TSharedRef<FAsyncPackageState>* FindAsyncPackage = ActiveAsyncPackagesMap.Find(AsyncPackagePtr);
		uint64 ImportedAsyncPackagePtr = EventData.GetValue<uint64>("ImportedAsyncPackage");
		TSharedRef<FAsyncPackageState>* FindImportedAsyncPackage = ActiveAsyncPackagesMap.Find(ImportedAsyncPackagePtr);
		if (FindAsyncPackage && FindImportedAsyncPackage)
		{
			TSharedRef<FAsyncPackageState> AsyncPackageState = *FindAsyncPackage;
			TSharedRef<FAsyncPackageState> ImportedAsyncPackageState = *FindImportedAsyncPackage;
			check(AsyncPackageState->Request);
			PackageRequestAssociation(Context, ImportedAsyncPackageState, AsyncPackageState->Request.ToSharedRef());
		}
		break;
	}
	case RouteId_AsyncPackageRequestAssociation:
	{
		uint64 AsyncPackagePtr = EventData.GetValue<uint64>("AsyncPackage");
		TSharedRef<FAsyncPackageState>* FindAsyncPackageState = ActiveAsyncPackagesMap.Find(AsyncPackagePtr);
		uint64 RequestId = EventData.GetValue<uint64>("RequestId");
		TSharedRef<FRequestState>* FindRequestState = ActiveRequestsMap.Find(RequestId);
		if (FindAsyncPackageState && FindRequestState)
		{
			PackageRequestAssociation(Context, *FindAsyncPackageState, *FindRequestState);
		}
		break;
	}
	case RouteId_AsyncPackageLinkerAssociation:
	{
		uint64 LinkerPtr = EventData.GetValue<uint64>("Linker");
		TSharedRef<FLinkerState>* LinkerState = ActiveLinkersMap.Find(LinkerPtr);
		uint64 AsyncPackagePtr = EventData.GetValue<uint64>("AsyncPackage");
		TSharedRef<FAsyncPackageState>* AsyncPackageState = ActiveAsyncPackagesMap.Find(AsyncPackagePtr);
		if (LinkerState && AsyncPackageState)
		{
			(*AsyncPackageState)->LinkerState = *LinkerState;
			(*LinkerState)->AsyncPackageState = *AsyncPackageState;
			(*LinkerState)->PackageInfo = (*AsyncPackageState)->PackageInfo;
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
	}

	return true;
}

void FAsyncLoadingTraceAnalyzer::FThreadState::EnterPackageScope(double Time, const Trace::FPackageInfo* PackageInfo, ELoadTimeProfilerPackageEventType EventType)
{
	FScopeStackEntry& StackEntry = CpuScopeStack[CpuScopeStackDepth++];
	StackEntry.Event.Export = nullptr;
	StackEntry.Event.ExportEventType = LoadTimeProfilerObjectEventType_None;
	StackEntry.Event.Package = PackageInfo;
	StackEntry.Event.PackageEventType = EventType;
	CurrentEvent = StackEntry.Event;
	CpuTimeline->AppendBeginEvent(Time, StackEntry.Event);
}

void FAsyncLoadingTraceAnalyzer::FThreadState::EnterExportScope(double Time, const Trace::FPackageExportInfo* ExportInfo, ELoadTimeProfilerObjectEventType EventType)
{
	FScopeStackEntry& StackEntry = CpuScopeStack[CpuScopeStackDepth++];
	StackEntry.Event.Export = ExportInfo;
	StackEntry.Event.ExportEventType = EventType;
	StackEntry.Event.Package = ExportInfo ? ExportInfo->Package : nullptr;
	StackEntry.Event.PackageEventType = CurrentEvent.PackageEventType;
	if (EventType == LoadTimeProfilerObjectEventType_PostLoad && StackEntry.Event.PackageEventType == LoadTimeProfilerPackageEventType_None)
	{
		StackEntry.Event.PackageEventType = LoadTimeProfilerPackageEventType_DeferredPostLoad;
	}
	CurrentEvent = StackEntry.Event;
	CpuTimeline->AppendBeginEvent(Time, StackEntry.Event);
}

void FAsyncLoadingTraceAnalyzer::FThreadState::LeaveScope(double Time)
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

void FAsyncLoadingTraceAnalyzer::PackageRequestAssociation(const FOnEventContext& Context, TSharedRef<FAsyncPackageState> AsyncPackageState, TSharedRef<FRequestState> RequestState)
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

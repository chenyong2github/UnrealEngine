// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "LoadTimeTraceAnalysis.h"

#include "HAL/FileManager.h"
#include "Serialization/LoadTimeTrace.h"
#include "Trace/Trace.h"
#include "Model/LoadTimeProfilerPrivate.h"
#include "Analyzers/MiscTraceAnalysis.h"

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
			ThreadState->CpuTimeline = LoadTimeProfilerProvider.EditMainThreadCpuTimeline();
		}
		else if (ThreadId == AsyncLoadingThreadId)
		{
			ThreadState->CpuTimeline = LoadTimeProfilerProvider.EditAsyncLoadingThreadCpuTimeline();
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
	Builder.RouteEvent(RouteId_BeginLoadMap, "LoadTime", "BeginLoadMap");
	Builder.RouteEvent(RouteId_EndLoadMap, "LoadTime", "EndLoadMap");
	Builder.RouteEvent(RouteId_NewStreamableHandle, "LoadTime", "NewStreamableHandle");
	Builder.RouteEvent(RouteId_DestroyStreamableHandle, "LoadTime", "DestroyStreamableHandle");
	Builder.RouteEvent(RouteId_BeginLoadStreamableHandle, "LoadTime", "BeginLoadStreamableHandle");
	Builder.RouteEvent(RouteId_EndLoadStreamableHandle, "LoadTime", "EndLoadStreamableHandle");
	Builder.RouteEvent(RouteId_BeginWaitForStreamableHandle, "LoadTime", "BeginWaitForStreamableHandle");
	Builder.RouteEvent(RouteId_EndWaitForStreamableHandle, "LoadTime", "EndWaitForStreamableHandle");
	Builder.RouteEvent(RouteId_StreamableHandleRequestAssociation, "LoadTime", "StreamableHandleRequestAssociation");
	Builder.RouteEvent(RouteId_AsyncPackageRequestAssociation, "LoadTime", "AsyncPackageRequestAssociation");
	Builder.RouteEvent(RouteId_AsyncPackageLinkerAssociation, "LoadTime", "AsyncPackageLinkerAssociation");
	Builder.RouteEvent(RouteId_LinkerArchiveAssociation, "LoadTime", "LinkerArchiveAssociation");
	Builder.RouteEvent(RouteId_BeginAsyncPackageScope, "LoadTime", "BeginAsyncPackageScope");
	Builder.RouteEvent(RouteId_EndAsyncPackageScope, "LoadTime", "EndAsyncPackageScope");
	Builder.RouteEvent(RouteId_BeginFlushAsyncLoading, "LoadTime", "BeginFlushAsyncLoading");
	Builder.RouteEvent(RouteId_EndFlushAsyncLoading, "LoadTime", "EndFlushAsyncLoading");
	Builder.RouteEvent(RouteId_ClassInfo, "LoadTime", "ClassInfo");
}

void FAsyncLoadingTraceAnalyzer::OnAnalysisEnd()
{
	
}

void FAsyncLoadingTraceAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_StartAsyncLoading:
	{
		Trace::FAnalysisSessionEditScope _(Session);
		AsyncLoadingThreadId = EventData.GetValue("ThreadId").As<uint32>();
		LoadTimeProfilerProvider.SetAsyncLoadingThreadId(AsyncLoadingThreadId);
		break;
	}
	case RouteId_NewLinker:
	{
		uint64 LinkerPtr = EventData.GetValue("Linker").As<uint64>();
		//check(!ActiveLinkersMap.Contains(LinkerPtr));
		TSharedRef<FLinkerState> LinkerState = MakeShared<FLinkerState>();
		ActiveLinkersMap.Add(LinkerPtr, LinkerState);
		break;
	}
	case RouteId_DestroyLinker:
	{
		uint64 Ptr = EventData.GetValue("Linker").As<uint64>();
		//check(ActiveLinkersMap.Contains(Ptr));
		ActiveLinkersMap.Remove(Ptr);
		break;
	}
	case RouteId_PackageSummary:
	{
		uint64 LinkerPtr = EventData.GetValue("Linker").As<uint64>();
		TSharedRef<FLinkerState>* LinkerState = ActiveLinkersMap.Find(LinkerPtr);
		if (LinkerState && (*LinkerState)->PackageInfo)
		{
			Trace::FAnalysisSessionEditScope _(Session);
			Trace::FPackageSummaryInfo& Summary = (*LinkerState)->PackageInfo->Summary;
			Summary.TotalHeaderSize = EventData.GetValue("TotalHeaderSize").As<uint32>();
			Summary.NameCount = EventData.GetValue("NameCount").As<uint32>();
			Summary.ImportCount = EventData.GetValue("ImportCount").As<uint32>();
			Summary.ExportCount = EventData.GetValue("ExportCount").As<uint32>();
		}
		break;
	}
	case RouteId_BeginCreateExport:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		Trace::FPackageExportInfo& Export = LoadTimeProfilerProvider.CreateExport();
		Export.SerialOffset = EventData.GetValue("SerialOffset").As<uint64>();
		Export.SerialSize = EventData.GetValue("SerialSize").As<uint64>();
		Export.IsAsset = EventData.GetValue("IsAsset").As<bool>();
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		ThreadState->EnterExportScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>()), &Export, LoadTimeProfilerObjectEventType_Create);
		uint64 LinkerPtr = EventData.GetValue("Linker").As<uint64>();
		TSharedRef<FLinkerState>* LinkerState = ActiveLinkersMap.Find(LinkerPtr);
		if (LinkerState && (*LinkerState)->PackageInfo)
		{
			(*LinkerState)->PackageInfo->Exports.Add(&Export);
		}
		break;
	}
	case RouteId_EndCreateExport:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		uint64 ObjectPtr = EventData.GetValue("Object").As<uint64>();
		Trace::FPackageExportInfo* Export = ThreadState->GetCurrentExportScope();
		if (Export)
		{
			ExportsMap.Add(ObjectPtr, Export);
			const Trace::FClassInfo* ObjectClass = GetClassInfo(EventData.GetValue("Class").As<uint64>());
			Export->Class = ObjectClass;
		}
		ThreadState->LeaveScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>()));
		break;
	}
	case RouteId_BeginObjectScope:
	{
		uint64 ObjectPtr = EventData.GetValue("Object").As<uint64>();
		Trace::FPackageExportInfo** Export = ExportsMap.Find(ObjectPtr);
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		ELoadTimeProfilerObjectEventType EventType = static_cast<ELoadTimeProfilerObjectEventType>(EventData.GetValue("EventType").As<uint8>());
		{
			Trace::FAnalysisSessionEditScope _(Session);
			ThreadState->EnterExportScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>()), Export ? *Export : nullptr, EventType);
		}
		break;
	}
	case RouteId_EndObjectScope:
	{
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		{
			Trace::FAnalysisSessionEditScope _(Session);
			ThreadState->LeaveScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>()));
		}
		break;
	}
	case RouteId_BeginRequest:
	{
		uint64 RequestId = EventData.GetValue("RequestId").As<uint64>();
		//check(!ActiveRequestsMap.Contains(RequestId));
		TSharedRef<FRequestState> RequestState = MakeShared<FRequestState>();
		RequestState->Id = RequestId;
		RequestState->WallTimeStartCycle = EventData.GetValue("Cycle").As<uint64>();
		RequestState->WallTimeEndCycle = 0;
		Requests.Add(RequestState);
		ActiveRequestsMap.Add(RequestId, RequestState);
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		if (ThreadState->ActiveLoadMap)
		{
			ThreadState->ActiveLoadMap->Requests.Add(RequestState);
		}
		
		{
			/*Trace::FAnalysisSessionEditScope _(Session.Get());
			RequestTimelineEventTypeMap.Add(RequestState->Id, TimelineProvider->AddEventType(*RequestState->PackageName, 0));*/
		}
		
		break;
	}
	case RouteId_EndRequest:
	{
		uint64 RequestId = EventData.GetValue("RequestId").As<uint64>();
		TSharedRef<FRequestState>* RequestState = ActiveRequestsMap.Find(RequestId);
		if (RequestState)
		{
			(*RequestState)->WallTimeEndCycle = EventData.GetValue("Cycle").As<uint64>();
		}
		break;
	}
	case RouteId_BeginLoadMap:
	{
		TSharedRef<FLoadMapState> LoadMapState = MakeShared<FLoadMapState>();
		LoadMapState->Id = NextMapId++;
		LoadMapState->WallTimeStartCycle = EventData.GetValue("Cycle").As<uint64>();
		LoadMapState->WallTimeEndCycle = 0;
		LoadMapState->Name = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());
		Maps.Add(LoadMapState);
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		ThreadState->ActiveLoadMap = LoadMapState;
		break;
	}
	case RouteId_EndLoadMap:
	{
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		if (ThreadState->ActiveLoadMap)
		{
			ThreadState->ActiveLoadMap->WallTimeEndCycle = EventData.GetValue("Cycle").As<uint64>();
			ThreadState->ActiveLoadMap = nullptr;
		}
		break;
	}
	case RouteId_NewStreamableHandle:
	{
		uint64 Ptr = EventData.GetValue("StreamableHandle").As<uint64>();
		//check(!ActiveStreamableHandlesMap.Contains(Ptr));
		TSharedRef<FStreamableHandleState> StreamableHandleState = MakeShared<FStreamableHandleState>();
		StreamableHandleState->Id = NextStreamableHandleId++;
		StreamableHandleState->DebugName = FString((const char*)EventData.GetAttachment(), EventData.GetValue("DebugNameSize").As<uint16>());
		StreamableHandleState->WallTimeStartCycle = 0;
		StreamableHandleState->WallTimeEndCycle = 0;
		StreamableHandles.Add(StreamableHandleState);
		ActiveStreamableHandlesMap.Add(Ptr, StreamableHandleState);
		
		{
			/*Trace::FAnalysisSessionEditScope _(Session.Get());
			StreamableHandlesTimelineEventTypeMap.Add(StreamableHandleState->Id, TimelineProvider->AddEventType(*StreamableHandleState->DebugName, 0));*/
		}
		
		break;
	}
	case RouteId_DestroyStreamableHandle:
	{
		uint64 Ptr = EventData.GetValue("StreamableHandle").As<uint64>();
		ActiveStreamableHandlesMap.Remove(Ptr);
		break;
	}
	case RouteId_BeginLoadStreamableHandle:
	{
		uint64 Ptr = EventData.GetValue("StreamableHandle").As<uint64>();
		TSharedRef<FStreamableHandleState>* StreamableHandleState = ActiveStreamableHandlesMap.Find(Ptr);
		if (StreamableHandleState)
		{
			(*StreamableHandleState)->WallTimeStartCycle = EventData.GetValue("Cycle").As<uint64>();
		}
		break;
	}
	case RouteId_EndLoadStreamableHandle:
	{
		uint64 Ptr = EventData.GetValue("StreamableHandle").As<uint64>();
		TSharedRef<FStreamableHandleState>* StreamableHandleState = ActiveStreamableHandlesMap.Find(Ptr);
		if (StreamableHandleState)
		{
			(*StreamableHandleState)->WallTimeEndCycle = EventData.GetValue("Cycle").As<uint64>();
		}
		break;
	}
	case RouteId_BeginWaitForStreamableHandle:
	{
		uint64 Ptr = EventData.GetValue("StreamableHandle").As<uint64>();
		TSharedRef<FStreamableHandleState>* StreamableHandleState = ActiveStreamableHandlesMap.Find(Ptr);
		if (StreamableHandleState)
		{
			uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
			TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
			ThreadState->WaitForStreamableHandleHandle = *StreamableHandleState;
			ThreadState->WaitForStreamableHandleStartCycle = EventData.GetValue("Cycle").As<uint64>();
		}
		/*uint64 EventTypeId = StreamableHandlesTimelineEventTypeMap[ThreadState->WaitForStreamableHandleHandle->Id];

		{
			Trace::FAnalysisSessionEditScope _(Session.Get());
			BlockingRequestsTimeline->AppendBeginEvent(Context.SessionContext.TimestampFromCycle(ThreadState->WaitForStreamableHandleStartCycle), EventTypeId);
		}*/

		break;
	}
	case RouteId_EndWaitForStreamableHandle:
	{
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		ThreadState->WaitForStreamableHandleHandle = nullptr;

		/*{
			Trace::FAnalysisSessionEditScope _(Session.Get());
			BlockingRequestsTimeline->AppendEndEvent(Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>()));
		}*/

		break;
	}
	case RouteId_NewAsyncPackage:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint64 AsyncPackagePtr = EventData.GetValue("AsyncPackage").As<uint64>();
		//check(!ActivePackagesMap.Contains(AsyncPackagePtr));
		TSharedRef<FAsyncPackageState> AsyncPackageState = MakeShared<FAsyncPackageState>();
		AsyncPackageState->PackageInfo = &LoadTimeProfilerProvider.CreatePackage(reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
		ActiveAsyncPackagesMap.Add(AsyncPackagePtr, AsyncPackageState);
		break;
	}
	case RouteId_DestroyAsyncPackage:
	{
		uint64 AsyncPackagePtr = EventData.GetValue("AsyncPackage").As<uint64>();
		//check(ActiveAsyncPackagesMap.Contains(AsyncPackagePtr));
		ActiveAsyncPackagesMap.Remove(AsyncPackagePtr);
		break;
	}

	case RouteId_StreamableHandleRequestAssociation:
	{
		uint64 StreamableHandlePtr = EventData.GetValue("StreamableHandle").As<uint64>();
		TSharedRef<FStreamableHandleState>* StreamableHandleState = ActiveStreamableHandlesMap.Find(StreamableHandlePtr);
		uint64 RequestId = EventData.GetValue("RequestId").As<uint64>();
		TSharedRef<FRequestState>* RequestState = ActiveRequestsMap.Find(RequestId);
		if (StreamableHandleState && RequestState)
		{
			(*StreamableHandleState)->Requests.Add(*RequestState);
		}
		break;
	}
	case RouteId_AsyncPackageRequestAssociation:
	{
		uint64 AsyncPackagePtr = EventData.GetValue("AsyncPackage").As<uint64>();
		TSharedRef<FAsyncPackageState>* AsyncPackageState = ActiveAsyncPackagesMap.Find(AsyncPackagePtr);
		uint64 RequestId = EventData.GetValue("RequestId").As<uint64>();
		TSharedRef<FRequestState>* RequestState = ActiveRequestsMap.Find(RequestId);
		if (AsyncPackageState && RequestState)
		{
			(*RequestState)->AsyncPackages.Add(*AsyncPackageState);
			(*AsyncPackageState)->Requests.Add(*RequestState);
		}
		break;
	}
	case RouteId_AsyncPackageLinkerAssociation:
	{
		uint64 LinkerPtr = EventData.GetValue("Linker").As<uint64>();
		TSharedRef<FLinkerState>* LinkerState = ActiveLinkersMap.Find(LinkerPtr);
		uint64 AsyncPackagePtr = EventData.GetValue("AsyncPackage").As<uint64>();
		TSharedRef<FAsyncPackageState>* AsyncPackageState = ActiveAsyncPackagesMap.Find(AsyncPackagePtr);
		if (LinkerState && AsyncPackageState)
		{
			(*AsyncPackageState)->LinkerState = *LinkerState;
			(*LinkerState)->AsyncPackageState = *AsyncPackageState;
			(*LinkerState)->PackageInfo = (*AsyncPackageState)->PackageInfo;
		}
		break;
	}
	case RouteId_LinkerArchiveAssociation:
	{
		uint64 ArchivePtr = EventData.GetValue("Archive").As<uint64>();
		uint64 LinkerPtr = EventData.GetValue("Linker").As<uint64>();
		break;
	}
	case RouteId_BeginAsyncPackageScope:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint64 AsyncPackagePtr = EventData.GetValue("AsyncPackage").As<uint64>();
		TSharedRef<FAsyncPackageState>* AsyncPackageState = ActiveAsyncPackagesMap.Find(AsyncPackagePtr);
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		ELoadTimeProfilerPackageEventType EventType = static_cast<ELoadTimeProfilerPackageEventType>(EventData.GetValue("EventType").As<uint8>());
		ThreadState->EnterPackageScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>()), AsyncPackageState ? (*AsyncPackageState)->PackageInfo : nullptr, EventType);
		break;
	}
	case RouteId_EndAsyncPackageScope:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		uint64 Cycle = EventData.GetValue("Cycle").As<uint64>();
		ThreadState->LeaveScope(Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>()));
		break;
	}
	case RouteId_BeginFlushAsyncLoading:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		FlushAsyncLoadingRequestId = EventData.GetValue("RequestId").As<uint64>();
		FlushAsyncLoadingStartCycle = EventData.GetValue("Cycle").As<uint64>();
		if (FlushAsyncLoadingRequestId != INDEX_NONE)
		{
			//check(ActiveRequestsMap.Contains(FlushAsyncLoadingRequestId));
			/*uint64 EventTypeId = RequestTimelineEventTypeMap[FlushAsyncLoadingRequestId];
			BlockingRequestsTimeline->AppendBeginEvent(Context.SessionContext.TimestampFromCycle(FlushAsyncLoadingStartCycle), EventTypeId);*/
		}
		else
		{
			//BlockingRequestsTimeline->AppendBeginEvent(Context.SessionContext.TimestampFromCycle(FlushAsyncLoadingStartCycle), FlushAsyncLoadingEventId);
		}
		break;
	}
	case RouteId_EndFlushAsyncLoading:
	{
		uint64 Cycle = EventData.GetValue("Cycle").As<uint64>();
		//BlockingRequestsTimeline->AppendEndEvent(Context.SessionContext.TimestampFromCycle(Cycle));
		break;
	}
	case RouteId_ClassInfo:
	{
		Trace::FAnalysisSessionEditScope _(Session);

		uint64 ClassPtr = EventData.GetValue("Class").As<uint64>();
		const Trace::FClassInfo& ClassInfo = LoadTimeProfilerProvider.AddClassInfo(reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
		ClassInfosMap.Add(ClassPtr, &ClassInfo);
		break;
	}
	}
}

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Serialization/LoadTimeTrace.h"

#if LOADTIMEPROFILERTRACE_ENABLED

#include "LoadTimeTracePrivate.h"
#include "Trace/Trace.h"
#include "Misc/CString.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/CommandLine.h"

UE_TRACE_EVENT_BEGIN(LoadTime, StartAsyncLoading, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, SuspendAsyncLoading)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, ResumeAsyncLoading)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, NewLinker)
	UE_TRACE_EVENT_FIELD(const FLinkerLoad*, Linker)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, DestroyLinker)
	UE_TRACE_EVENT_FIELD(const FLinkerLoad*, Linker)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, PackageSummary)
	UE_TRACE_EVENT_FIELD(const FLinkerLoad*, Linker)
	UE_TRACE_EVENT_FIELD(uint32, TotalHeaderSize)
	UE_TRACE_EVENT_FIELD(uint32, NameCount)
	UE_TRACE_EVENT_FIELD(uint32, ImportCount)
	UE_TRACE_EVENT_FIELD(uint32, ExportCount)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, BeginCreateExport)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const FLinkerLoad*, Linker)
	UE_TRACE_EVENT_FIELD(uint64, SerialOffset)
	UE_TRACE_EVENT_FIELD(uint64, SerialSize)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
	UE_TRACE_EVENT_FIELD(bool, IsAsset)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, EndCreateExport)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const UObject*, Object)
	UE_TRACE_EVENT_FIELD(const UClass*, Class)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, BeginObjectScope)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const UObject*, Object)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, EndObjectScope)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, BeginRequest)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, RequestId)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, EndRequest)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, RequestId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, NewAsyncPackage)
	UE_TRACE_EVENT_FIELD(const FAsyncPackage*, AsyncPackage)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, DestroyAsyncPackage)
	UE_TRACE_EVENT_FIELD(const FAsyncPackage*, AsyncPackage)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, AsyncPackageRequestAssociation)
	UE_TRACE_EVENT_FIELD(const FAsyncPackage*, AsyncPackage)
	UE_TRACE_EVENT_FIELD(uint64, RequestId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, AsyncPackageImportDependency)
	UE_TRACE_EVENT_FIELD(const FAsyncPackage*, AsyncPackage)
	UE_TRACE_EVENT_FIELD(const FAsyncPackage*, ImportedAsyncPackage)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, AsyncPackageLinkerAssociation)
	UE_TRACE_EVENT_FIELD(const FAsyncPackage*, AsyncPackage)
	UE_TRACE_EVENT_FIELD(const FLinkerLoad*, Linker)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, BeginAsyncPackageScope)
	UE_TRACE_EVENT_FIELD(const FAsyncPackage*, AsyncPackage)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, EndAsyncPackageScope)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, ClassInfo, Always)
	UE_TRACE_EVENT_FIELD(const UClass*, Class)
UE_TRACE_EVENT_END()

void FLoadTimeProfilerTracePrivate::Init()
{
	FLoadTimeProfilerTrace::InitInternal();
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, StartAsyncLoading);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, SuspendAsyncLoading);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, ResumeAsyncLoading);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, NewLinker);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, DestroyLinker);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, PackageSummary);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, BeginCreateExport);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, EndCreateExport);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, BeginObjectScope);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, EndObjectScope);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, BeginRequest);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, EndRequest);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, NewAsyncPackage);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, DestroyAsyncPackage);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, AsyncPackageRequestAssociation);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, AsyncPackageImportDependency);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, AsyncPackageLinkerAssociation);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, BeginAsyncPackageScope);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, EndAsyncPackageScope);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, ClassInfo);
	if (FParse::Param(FCommandLine::Get(), TEXT("loadtimetrace")))
	{
		Trace::ToggleEvent(TEXT("LoadTime"), true);
	}
}

void FLoadTimeProfilerTracePrivate::OutputStartAsyncLoading()
{
	UE_TRACE_LOG(LoadTime, StartAsyncLoading)
		<< StartAsyncLoading.Cycle(FPlatformTime::Cycles64())
		<< StartAsyncLoading.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

PRAGMA_DISABLE_SHADOW_VARIABLE_WARNINGS
void FLoadTimeProfilerTracePrivate::OutputSuspendAsyncLoading()
{
	UE_TRACE_LOG(LoadTime, SuspendAsyncLoading)
		<< SuspendAsyncLoading.Cycle(FPlatformTime::Cycles64());
}

void FLoadTimeProfilerTracePrivate::OutputResumeAsyncLoading()
{
	UE_TRACE_LOG(LoadTime, ResumeAsyncLoading)
		<< ResumeAsyncLoading.Cycle(FPlatformTime::Cycles64());
}
PRAGMA_ENABLE_SHADOW_VARIABLE_WARNINGS

void FLoadTimeProfilerTracePrivate::OutputNewLinker(const FLinkerLoad* Linker)
{
	UE_TRACE_LOG(LoadTime, NewLinker)
		<< NewLinker.Linker(Linker);
}

void FLoadTimeProfilerTracePrivate::OutputDestroyLinker(const FLinkerLoad* Linker)
{
	UE_TRACE_LOG(LoadTime, DestroyLinker)
		<< DestroyLinker.Linker(Linker);
}

void FLoadTimeProfilerTracePrivate::OutputBeginRequest(uint64 RequestId)
{
	UE_TRACE_LOG(LoadTime, BeginRequest)
		<< BeginRequest.Cycle(FPlatformTime::Cycles64())
		<< BeginRequest.RequestId(RequestId)
		<< BeginRequest.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FLoadTimeProfilerTracePrivate::OutputEndRequest(uint64 RequestId)
{
	UE_TRACE_LOG(LoadTime, EndRequest)
		<< EndRequest.Cycle(FPlatformTime::Cycles64())
		<< EndRequest.RequestId(RequestId);
}

void FLoadTimeProfilerTracePrivate::OutputNewAsyncPackage(const FAsyncPackage* AsyncPackage, const TCHAR* PackageName)
{
	uint16 NameSize = (FCString::Strlen(PackageName) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(LoadTime, NewAsyncPackage, NameSize)
		<< NewAsyncPackage.AsyncPackage(AsyncPackage)
		<< NewAsyncPackage.Attachment(PackageName, NameSize);
}

void FLoadTimeProfilerTracePrivate::OutputDestroyAsyncPackage(const FAsyncPackage* AsyncPackage)
{
	UE_TRACE_LOG(LoadTime, DestroyAsyncPackage)
		<< DestroyAsyncPackage.AsyncPackage(AsyncPackage);
}

void FLoadTimeProfilerTracePrivate::OutputPackageSummary(const FLinkerLoad* Linker, uint32 TotalHeaderSize, uint32 NameCount, uint32 ImportCount, uint32 ExportCount)
{
	UE_TRACE_LOG(LoadTime, PackageSummary)
		<< PackageSummary.Linker(Linker)
		<< PackageSummary.TotalHeaderSize(TotalHeaderSize)
		<< PackageSummary.NameCount(NameCount)
		<< PackageSummary.ImportCount(ImportCount)
		<< PackageSummary.ExportCount(ExportCount);
}

void FLoadTimeProfilerTracePrivate::OutputAsyncPackageRequestAssociation(const FAsyncPackage* AsyncPackage, uint64 RequestId)
{
	UE_TRACE_LOG(LoadTime, AsyncPackageRequestAssociation)
		<< AsyncPackageRequestAssociation.AsyncPackage(AsyncPackage)
		<< AsyncPackageRequestAssociation.RequestId(RequestId);
}

void FLoadTimeProfilerTracePrivate::OutputAsyncPackageImportDependency(const FAsyncPackage* Package, const FAsyncPackage* ImportedPackage)
{
	UE_TRACE_LOG(LoadTime, AsyncPackageImportDependency)
		<< AsyncPackageImportDependency.AsyncPackage(Package)
		<< AsyncPackageImportDependency.ImportedAsyncPackage(ImportedPackage);
}

void FLoadTimeProfilerTracePrivate::OutputAsyncPackageLinkerAssociation(const FAsyncPackage* AsyncPackage, const FLinkerLoad* Linker)
{
	UE_TRACE_LOG(LoadTime, AsyncPackageLinkerAssociation)
		<< AsyncPackageLinkerAssociation.AsyncPackage(AsyncPackage)
		<< AsyncPackageLinkerAssociation.Linker(Linker);
}

void FLoadTimeProfilerTracePrivate::OutputClassInfo(const UClass* Class, const TCHAR* Name)
{
	uint16 NameSize = (FCString::Strlen(Name) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(LoadTime, ClassInfo, NameSize)
		<< ClassInfo.Class(Class)
		<< ClassInfo.Attachment(Name, NameSize);
}

FLoadTimeProfilerTracePrivate::FAsyncPackageScope::FAsyncPackageScope(const FAsyncPackage* AsyncPackage, ELoadTimeProfilerPackageEventType EventType)
{
	UE_TRACE_LOG(LoadTime, BeginAsyncPackageScope)
		<< BeginAsyncPackageScope.AsyncPackage(AsyncPackage)
		<< BeginAsyncPackageScope.Cycle(FPlatformTime::Cycles64())
		<< BeginAsyncPackageScope.ThreadId(FPlatformTLS::GetCurrentThreadId())
		<< BeginAsyncPackageScope.EventType(EventType);
}

FLoadTimeProfilerTracePrivate::FAsyncPackageScope::~FAsyncPackageScope()
{
	UE_TRACE_LOG(LoadTime, EndAsyncPackageScope)
		<< EndAsyncPackageScope.Cycle(FPlatformTime::Cycles64())
		<< EndAsyncPackageScope.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

FLoadTimeProfilerTracePrivate::FCreateExportScope::FCreateExportScope(const FLinkerLoad* Linker, uint64 SerialOffset, uint64 SerialSize, bool IsAsset, const UObject* const* InObject)
	: Object(InObject)
{
	UE_TRACE_LOG(LoadTime, BeginCreateExport)
		<< BeginCreateExport.Cycle(FPlatformTime::Cycles64())
		<< BeginCreateExport.Linker(Linker)
		<< BeginCreateExport.SerialOffset(SerialOffset)
		<< BeginCreateExport.SerialSize(SerialSize)
		<< BeginCreateExport.ThreadId(FPlatformTLS::GetCurrentThreadId())
		<< BeginCreateExport.IsAsset(IsAsset);
}

FLoadTimeProfilerTracePrivate::FCreateExportScope::~FCreateExportScope()
{
	UE_TRACE_LOG(LoadTime, EndCreateExport)
		<< EndCreateExport.Cycle(FPlatformTime::Cycles64())
		<< EndCreateExport.Object(*Object)
		<< EndCreateExport.Class(*Object ? (*Object)->GetClass() : nullptr)
		<< EndCreateExport.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

FLoadTimeProfilerTracePrivate::FObjectScope::FObjectScope(const UObject* Object, ELoadTimeProfilerObjectEventType EventType)
{
	UE_TRACE_LOG(LoadTime, BeginObjectScope)
		<< BeginObjectScope.Cycle(FPlatformTime::Cycles64())
		<< BeginObjectScope.Object(Object)
		<< BeginObjectScope.ThreadId(FPlatformTLS::GetCurrentThreadId())
		<< BeginObjectScope.EventType(EventType);
}

FLoadTimeProfilerTracePrivate::FObjectScope::~FObjectScope()
{
	UE_TRACE_LOG(LoadTime, EndObjectScope)
		<< EndObjectScope.Cycle(FPlatformTime::Cycles64())
		<< EndObjectScope.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

#endif
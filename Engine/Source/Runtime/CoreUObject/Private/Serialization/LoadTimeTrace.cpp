// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Serialization/LoadTimeTrace.h"

#if LOADTIMEPROFILERTRACE_ENABLED

#include "LoadTimeTracePrivate.h"
#include "Trace/Trace.h"
#include "Misc/CString.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "UObject/NameTypes.h"
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

UE_TRACE_EVENT_BEGIN(LoadTime, PackageSummary)
	UE_TRACE_EVENT_FIELD(const void*, AsyncPackage)
	UE_TRACE_EVENT_FIELD(uint32, TotalHeaderSize)
	UE_TRACE_EVENT_FIELD(uint32, ImportCount)
	UE_TRACE_EVENT_FIELD(uint32, ExportCount)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, BeginCreateExport)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const void*, AsyncPackage)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, EndCreateExport)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const UObject*, Object)
	UE_TRACE_EVENT_FIELD(const UClass*, Class)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, BeginSerializeExport)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const UObject*, Object)
	UE_TRACE_EVENT_FIELD(uint64, SerialSize)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, EndSerializeExport)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, BeginPostLoadExport)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const UObject*, Object)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, EndPostLoadExport)
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
	UE_TRACE_EVENT_FIELD(const void*, AsyncPackage)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, BeginLoadAsyncPackage)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const void*, AsyncPackage)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, EndLoadAsyncPackage)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const void*, AsyncPackage)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, DestroyAsyncPackage)
	UE_TRACE_EVENT_FIELD(const void*, AsyncPackage)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, AsyncPackageRequestAssociation)
	UE_TRACE_EVENT_FIELD(const void*, AsyncPackage)
	UE_TRACE_EVENT_FIELD(uint64, RequestId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, AsyncPackageImportDependency)
	UE_TRACE_EVENT_FIELD(const void*, AsyncPackage)
	UE_TRACE_EVENT_FIELD(const void*, ImportedAsyncPackage)
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
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, PackageSummary);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, BeginCreateExport);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, EndCreateExport);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, BeginSerializeExport);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, EndSerializeExport);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, BeginPostLoadExport);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, EndPostLoadExport);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, BeginRequest);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, EndRequest);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, NewAsyncPackage);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, BeginLoadAsyncPackage);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, EndLoadAsyncPackage);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, DestroyAsyncPackage);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, AsyncPackageRequestAssociation);
	UE_TRACE_EVENT_IS_ENABLED(LoadTime, AsyncPackageImportDependency);
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

void FLoadTimeProfilerTracePrivate::OutputNewAsyncPackage(const void* AsyncPackage, const FName& PackageName)
{
	TCHAR Buffer[FName::StringBufferSize];
	uint16 NameSize = (PackageName.ToString(Buffer) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(LoadTime, NewAsyncPackage, NameSize)
		<< NewAsyncPackage.AsyncPackage(AsyncPackage)
		<< NewAsyncPackage.Attachment(Buffer, NameSize);
}

void FLoadTimeProfilerTracePrivate::OutputBeginLoadAsyncPackage(const void* AsyncPackage)
{
	UE_TRACE_LOG(LoadTime, BeginLoadAsyncPackage)
		<< BeginLoadAsyncPackage.Cycle(FPlatformTime::Cycles64())
		<< BeginLoadAsyncPackage.AsyncPackage(AsyncPackage);
}

void FLoadTimeProfilerTracePrivate::OutputEndLoadAsyncPackage(const void* AsyncPackage)
{
	UE_TRACE_LOG(LoadTime, EndLoadAsyncPackage)
		<< EndLoadAsyncPackage.Cycle(FPlatformTime::Cycles64())
		<< EndLoadAsyncPackage.AsyncPackage(AsyncPackage);
}

void FLoadTimeProfilerTracePrivate::OutputDestroyAsyncPackage(const void* AsyncPackage)
{
	UE_TRACE_LOG(LoadTime, DestroyAsyncPackage)
		<< DestroyAsyncPackage.AsyncPackage(AsyncPackage);
}

void FLoadTimeProfilerTracePrivate::OutputPackageSummary(const void* AsyncPackage, uint32 TotalHeaderSize, uint32 ImportCount, uint32 ExportCount)
{
	UE_TRACE_LOG(LoadTime, PackageSummary)
		<< PackageSummary.AsyncPackage(AsyncPackage)
		<< PackageSummary.TotalHeaderSize(TotalHeaderSize)
		<< PackageSummary.ImportCount(ImportCount)
		<< PackageSummary.ExportCount(ExportCount);
}

void FLoadTimeProfilerTracePrivate::OutputAsyncPackageRequestAssociation(const void* AsyncPackage, uint64 RequestId)
{
	UE_TRACE_LOG(LoadTime, AsyncPackageRequestAssociation)
		<< AsyncPackageRequestAssociation.AsyncPackage(AsyncPackage)
		<< AsyncPackageRequestAssociation.RequestId(RequestId);
}

void FLoadTimeProfilerTracePrivate::OutputAsyncPackageImportDependency(const void* Package, const void* ImportedPackage)
{
	UE_TRACE_LOG(LoadTime, AsyncPackageImportDependency)
		<< AsyncPackageImportDependency.AsyncPackage(Package)
		<< AsyncPackageImportDependency.ImportedAsyncPackage(ImportedPackage);
}

void FLoadTimeProfilerTracePrivate::OutputClassInfo(const UClass* Class, const FName& Name)
{
	TCHAR Buffer[FName::StringBufferSize];
	uint16 NameSize = (Name.ToString(Buffer) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(LoadTime, ClassInfo, NameSize)
		<< ClassInfo.Class(Class)
		<< ClassInfo.Attachment(Buffer, NameSize);
}

void FLoadTimeProfilerTracePrivate::OutputClassInfo(const UClass* Class, const TCHAR* Name)
{
	uint16 NameSize = (FCString::Strlen(Name) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(LoadTime, ClassInfo, NameSize)
		<< ClassInfo.Class(Class)
		<< ClassInfo.Attachment(Name, NameSize);
}

FLoadTimeProfilerTracePrivate::FCreateExportScope::FCreateExportScope(const void* AsyncPackage, const UObject* const* InObject)
	: Object(InObject)
{
	UE_TRACE_LOG(LoadTime, BeginCreateExport)
		<< BeginCreateExport.Cycle(FPlatformTime::Cycles64())
		<< BeginCreateExport.AsyncPackage(AsyncPackage)
		<< BeginCreateExport.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

FLoadTimeProfilerTracePrivate::FCreateExportScope::~FCreateExportScope()
{
	UE_TRACE_LOG(LoadTime, EndCreateExport)
		<< EndCreateExport.Cycle(FPlatformTime::Cycles64())
		<< EndCreateExport.Object(*Object)
		<< EndCreateExport.Class(*Object ? (*Object)->GetClass() : nullptr)
		<< EndCreateExport.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

FLoadTimeProfilerTracePrivate::FSerializeExportScope::FSerializeExportScope(const UObject* Object, uint64 SerialSize)
{
	UE_TRACE_LOG(LoadTime, BeginSerializeExport)
		<< BeginSerializeExport.Cycle(FPlatformTime::Cycles64())
		<< BeginSerializeExport.Object(Object)
		<< BeginSerializeExport.SerialSize(SerialSize)
		<< BeginSerializeExport.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

FLoadTimeProfilerTracePrivate::FSerializeExportScope::~FSerializeExportScope()
{
	UE_TRACE_LOG(LoadTime, EndSerializeExport)
		<< EndSerializeExport.Cycle(FPlatformTime::Cycles64())
		<< EndSerializeExport.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

FLoadTimeProfilerTracePrivate::FPostLoadExportScope::FPostLoadExportScope(const UObject* Object)
{
	UE_TRACE_LOG(LoadTime, BeginPostLoadExport)
		<< BeginPostLoadExport.Cycle(FPlatformTime::Cycles64())
		<< BeginPostLoadExport.Object(Object)
		<< BeginPostLoadExport.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

FLoadTimeProfilerTracePrivate::FPostLoadExportScope::~FPostLoadExportScope()
{
	UE_TRACE_LOG(LoadTime, EndPostLoadExport)
		<< EndPostLoadExport.Cycle(FPlatformTime::Cycles64())
		<< EndPostLoadExport.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

#endif

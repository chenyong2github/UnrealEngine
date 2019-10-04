// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/LoadTimeTrace.h"

#if LOADTIMEPROFILERTRACE_ENABLED

#include "UObject/UObjectGlobals.h"

class FArchive;
struct FAsyncPackage;
class FLinkerLoad;
class UObject;
class UPackage;

struct FLoadTimeProfilerTracePrivate
{
	static void Init();
	static void OutputStartAsyncLoading();
	static void OutputSuspendAsyncLoading();
	static void OutputResumeAsyncLoading();
	static void OutputNewLinker(const FLinkerLoad* Linker);
	static void OutputDestroyLinker(const FLinkerLoad* Linker);
	static void OutputNewAsyncPackage(const FAsyncPackage* AsyncPackage, const TCHAR* PackageName);
	static void OutputDestroyAsyncPackage(const FAsyncPackage* AsyncPackage);
	static void OutputBeginRequest(uint64 RequestId);
	static void OutputEndRequest(uint64 RequestId);
	static void OutputPackageSummary(const FLinkerLoad* Linker, uint32 TotalHeaderSize, uint32 NameCount, uint32 ImportCount, uint32 ExportCount);
	static void OutputAsyncPackageImportDependency(const FAsyncPackage* Package, const FAsyncPackage* ImportedPackage);
	static void OutputAsyncPackageRequestAssociation(const FAsyncPackage* AsyncPackage, uint64 RequestId);
	static void OutputAsyncPackageLinkerAssociation(const FAsyncPackage* AsyncPackage, const FLinkerLoad* Linker);
	static void OutputClassInfo(const UClass* Class, const TCHAR* Name);

	struct FAsyncPackageScope
	{
		FAsyncPackageScope(const FAsyncPackage* AsyncPackage, ELoadTimeProfilerPackageEventType EventType);
		~FAsyncPackageScope();
	};

	struct FCreateExportScope
	{
		FCreateExportScope(const FLinkerLoad* Linker, uint64 SerialOffset, uint64 SerialSize, bool IsAsset, const UObject* const* InObject);
		~FCreateExportScope();

	private:
		const UObject* const* Object;
	};

	struct FObjectScope
	{
		FObjectScope(const UObject* Object, ELoadTimeProfilerObjectEventType EventType);
		~FObjectScope();
	};

	struct FLinkerScope
	{
		FLinkerScope(const FLinkerLoad* Linker);
		~FLinkerScope();
	};
};

#define TRACE_LOADTIME_START_ASYNC_LOADING() \
	FLoadTimeProfilerTracePrivate::OutputStartAsyncLoading();

#define TRACE_LOADTIME_SUSPEND_ASYNC_LOADING() \
	FLoadTimeProfilerTracePrivate::OutputSuspendAsyncLoading();

#define TRACE_LOADTIME_RESUME_ASYNC_LOADING() \
	FLoadTimeProfilerTracePrivate::OutputResumeAsyncLoading();

#define TRACE_LOADTIME_NEW_LINKER(Linker) \
	FLoadTimeProfilerTracePrivate::OutputNewLinker(Linker);

#define TRACE_LOADTIME_DESTROY_LINKER(Linker) \
	FLoadTimeProfilerTracePrivate::OutputDestroyLinker(Linker);

#define TRACE_LOADTIME_PACKAGE_SUMMARY(Linker, TotalHeaderSize, NameCount, ImportCount, ExportCount) \
	FLoadTimeProfilerTracePrivate::OutputPackageSummary(Linker, TotalHeaderSize, NameCount, ImportCount, ExportCount);

#define TRACE_LOADTIME_BEGIN_REQUEST(RequestId) \
	FLoadTimeProfilerTracePrivate::OutputBeginRequest(RequestId);

#define TRACE_LOADTIME_END_REQUEST(RequestId) \
	FLoadTimeProfilerTracePrivate::OutputEndRequest(RequestId);

#define TRACE_LOADTIME_NEW_ASYNC_PACKAGE(AsyncPackage, PackageName) \
	FLoadTimeProfilerTracePrivate::OutputNewAsyncPackage(AsyncPackage, PackageName)

#define TRACE_LOADTIME_DESTROY_ASYNC_PACKAGE(AsyncPackage) \
	FLoadTimeProfilerTracePrivate::OutputDestroyAsyncPackage(AsyncPackage);

#define TRACE_LOADTIME_ASYNC_PACKAGE_REQUEST_ASSOCIATION(AsyncPackage, RequestId) \
	FLoadTimeProfilerTracePrivate::OutputAsyncPackageRequestAssociation(AsyncPackage, RequestId);

#define TRACE_LOADTIME_ASYNC_PACKAGE_LINKER_ASSOCIATION(AsyncPackage, Linker) \
	FLoadTimeProfilerTracePrivate::OutputAsyncPackageLinkerAssociation(AsyncPackage, Linker);

#define TRACE_LOADTIME_ASYNC_PACKAGE_IMPORT_DEPENDENCY(AsyncPackage, ImportedAsyncPackage) \
	FLoadTimeProfilerTracePrivate::OutputAsyncPackageImportDependency(AsyncPackage, ImportedAsyncPackage);

#define TRACE_LOADTIME_ASYNC_PACKAGE_SCOPE(AsyncPackage, EventType) \
	FLoadTimeProfilerTracePrivate::FAsyncPackageScope __LoadTimeTraceAsyncPackageScope(AsyncPackage, EventType);

#define TRACE_LOADTIME_CREATE_EXPORT_SCOPE(Linker, Object, SerialOffset, SerialSize, IsAsset) \
	FLoadTimeProfilerTracePrivate::FCreateExportScope __LoadTimeTraceCreateExportScope(Linker, SerialOffset, SerialSize, IsAsset, Object);

#define TRACE_LOADTIME_OBJECT_SCOPE(Object, EventType) \
	FLoadTimeProfilerTracePrivate::FObjectScope __LoadTimeTraceObjectScope(Object, EventType);

#define TRACE_LOADTIME_CLASS_INFO(Class, Name) \
	FLoadTimeProfilerTracePrivate::OutputClassInfo(Class, Name);

#else

#define TRACE_LOADTIME_START_ASYNC_LOADING(...)
#define TRACE_LOADTIME_SUSPEND_ASYNC_LOADING(...)
#define TRACE_LOADTIME_RESUME_ASYNC_LOADING(...)
#define TRACE_LOADTIME_NEW_PACKAGE(...)
#define TRACE_LOADTIME_NEW_LINKER(...)
#define TRACE_LOADTIME_DESTROY_LINKER(...)
#define TRACE_LOADTIME_PACKAGE_SUMMARY(...)
#define TRACE_LOADTIME_BEGIN_REQUEST(...)
#define TRACE_LOADTIME_END_REQUEST(...)
#define TRACE_LOADTIME_NEW_ASYNC_PACKAGE(...)
#define TRACE_LOADTIME_DESTROY_ASYNC_PACKAGE(...)
#define TRACE_LOADTIME_ASYNC_PACKAGE_REQUEST_ASSOCIATION(...)
#define TRACE_LOADTIME_ASYNC_PACKAGE_LINKER_ASSOCIATION(...)
#define TRACE_LOADTIME_ASYNC_PACKAGE_IMPORT_DEPENDENCY(...)
#define TRACE_LOADTIME_ASYNC_PACKAGE_SCOPE(...)
#define TRACE_LOADTIME_TIMER_SCOPE(...)
#define TRACE_LOADTIME_CREATE_EXPORT_SCOPE(...)
#define TRACE_LOADTIME_OBJECT_SCOPE(...)
#define TRACE_LOADTIME_CLASS_INFO(...)

#endif
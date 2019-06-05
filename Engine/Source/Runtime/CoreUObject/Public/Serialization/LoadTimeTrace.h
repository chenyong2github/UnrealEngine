// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Trace.h"

#if UE_TRACE_ENABLED && !UE_BUILD_SHIPPING
#define LOADTIMEPROFILERTRACE_ENABLED 1
#else
#define LOADTIMEPROFILERTRACE_ENABLED 0
#endif

enum ELoadTimeProfilerPackageEventType
{
	LoadTimeProfilerPackageEventType_CreateLinker,
	LoadTimeProfilerPackageEventType_FinishLinker,
	LoadTimeProfilerPackageEventType_StartImportPackages,
	LoadTimeProfilerPackageEventType_SetupImports,
	LoadTimeProfilerPackageEventType_SetupExports,
	LoadTimeProfilerPackageEventType_ProcessImportsAndExports,
	LoadTimeProfilerPackageEventType_ExportsDone,
	LoadTimeProfilerPackageEventType_PostLoadWait,
	LoadTimeProfilerPackageEventType_StartPostLoad,
	LoadTimeProfilerPackageEventType_Tick,
	LoadTimeProfilerPackageEventType_Finish,
	LoadTimeProfilerPackageEventType_DeferredPostLoad,
	LoadTimeProfilerPackageEventType_None,
};

enum ELoadTimeProfilerObjectEventType
{
	LoadTimeProfilerObjectEventType_Create,
	LoadTimeProfilerObjectEventType_Serialize,
	LoadTimeProfilerObjectEventType_PostLoad,
	LoadTimeProfilerObjectEventType_None
};

#if LOADTIMEPROFILERTRACE_ENABLED

struct FStreamableHandle;

struct FLoadTimeProfilerTrace
{
	COREUOBJECT_API static void OutputNewStreamableHandle(const FStreamableHandle* StreamableHandle, const TCHAR* DebugName, bool IsCombined);
	COREUOBJECT_API static void OutputDestroyStreamableHandle(const FStreamableHandle* StreamableHandle);
	COREUOBJECT_API static void OutputBeginLoadStreamableHandle(const FStreamableHandle* StreamableHandle);
	COREUOBJECT_API static void OutputEndLoadStreamableHandle(const FStreamableHandle* StreamableHandle);
	COREUOBJECT_API static void OutputStreamableHandleRequestAssociation(const FStreamableHandle* StreamableHandle, uint64 RequestId);

	struct FLoadMapScope
	{
		COREUOBJECT_API FLoadMapScope(const TCHAR* Name);
		COREUOBJECT_API ~FLoadMapScope();
	};

	struct FWaitForStreamableHandleScope
	{
		COREUOBJECT_API FWaitForStreamableHandleScope(const FStreamableHandle* StreamableHandle);
		COREUOBJECT_API ~FWaitForStreamableHandleScope();
	};
};

#define TRACE_LOADTIME_LOAD_MAP_SCOPE(Name) \
	FLoadTimeProfilerTrace::FLoadMapScope __LoadTimeTraceLoadMapScope(Name);

#define TRACE_LOADTIME_NEW_STREAMABLE_HANDLE(StreamableHandle, DebugName, IsCombined) \
	FLoadTimeProfilerTrace::OutputNewStreamableHandle(StreamableHandle, DebugName, IsCombined);

#define TRACE_LOADTIME_DESTROY_STREAMABLE_HANDLE(StreamableHandle) \
	FLoadTimeProfilerTrace::OutputDestroyStreamableHandle(StreamableHandle);

#define TRACE_LOADTIME_BEGIN_LOAD_STREAMABLE_HANDLE(StreamableHandle) \
	FLoadTimeProfilerTrace::OutputBeginLoadStreamableHandle(StreamableHandle);

#define TRACE_LOADTIME_END_LOAD_STREAMABLE_HANDLE(StreamableHandle) \
	FLoadTimeProfilerTrace::OutputEndLoadStreamableHandle(StreamableHandle);

#define TRACE_LOADTIME_STREAMABLE_HANDLE_REQUEST_ASSOCIATION(StreamableHandle, RequestId) \
	FLoadTimeProfilerTrace::OutputStreamableHandleRequestAssociation(StreamableHandle, RequestId);

#define TRACE_LOADTIME_WAIT_FOR_STREAMABLE_HANDLE_SCOPE(StreamableHandle) \
	FLoadTimeProfilerTrace::FWaitForStreamableHandleScope __LoadTimeTraceWaitForStreamableHandleScope(StreamableHandle);

#else
#define TRACE_LOADTIME_LOAD_MAP_SCOPE(...)
#define TRACE_LOADTIME_NEW_STREAMABLE_HANDLE(...)
#define TRACE_LOADTIME_DESTROY_STREAMABLE_HANDLE(...)
#define TRACE_LOADTIME_BEGIN_LOAD_STREAMABLE_HANDLE(...)
#define TRACE_LOADTIME_END_LOAD_STREAMABLE_HANDLE(...)
#define TRACE_LOADTIME_STREAMABLE_HANDLE_REQUEST_ASSOCIATION(...)
#define TRACE_LOADTIME_WAIT_FOR_STREAMABLE_HANDLE_SCOPE(...)
#endif

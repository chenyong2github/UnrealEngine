// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Config.h"

#if UE_TRACE_ENABLED && PLATFORM_WINDOWS && !UE_BUILD_SHIPPING
#define PLATFORMFILETRACE_ENABLED 1
#else
#define PLATFORMFILETRACE_ENABLED 0
#endif

#if PLATFORMFILETRACE_ENABLED

struct FPlatformFileTrace
{
	CORE_API static void Init(const TCHAR* CmdLine);

	static void BeginOpen(const TCHAR* Path);
	static void EndOpen(uint64 FileHandle);
	static void BeginClose(uint64 FileHandle);
	static void EndClose();
	static void BeginRead(uint64 ReadHandle, uint64 FileHandle, uint64 Offset, uint64 Size);
	static void EndRead(uint64 ReadHandle, uint64 SizeRead);
	static void BeginWrite(uint64 WriteHandle, uint64 FileHandle, uint64 Offset, uint64 Size);
	static void EndWrite(uint64 WriteHandle, uint64 SizeWritten);
};

#define TRACE_PLATFORMFILE_INIT(CmdLine) \
	FPlatformFileTrace::Init(CmdLine);

#define TRACE_PLATFORMFILE_BEGIN_OPEN(Path) \
	FPlatformFileTrace::BeginOpen(Path);

#define TRACE_PLATFORMFILE_END_OPEN(FileHandle) \
	FPlatformFileTrace::EndOpen(uint64(FileHandle));

#define TRACE_PLATFORMFILE_BEGIN_CLOSE(FileHandle) \
	FPlatformFileTrace::BeginClose(uint64(FileHandle));

#define TRACE_PLATFORMFILE_END_CLOSE() \
	FPlatformFileTrace::EndClose();

#define TRACE_PLATFORMFILE_BEGIN_READ(ReadHandle, FileHandle, Offset, Size) \
	FPlatformFileTrace::BeginRead(uint64(ReadHandle), uint64(FileHandle), Offset, Size);

#define TRACE_PLATFORMFILE_END_READ(ReadHandle, SizeRead) \
	FPlatformFileTrace::EndRead(uint64(ReadHandle), SizeRead);

#define TRACE_PLATFORMFILE_BEGIN_WRITE(WriteHandle, FileHandle, Offset, Size) \
	FPlatformFileTrace::BeginWrite(uint64(WriteHandle), uint64(FileHandle), Offset, Size);

#define TRACE_PLATFORMFILE_END_WRITE(WriteHandle, SizeWritten) \
	FPlatformFileTrace::EndWrite(uint64(WriteHandle), SizeWritten);

#else

#define TRACE_PLATFORMFILE_INIT(CmdLine)
#define TRACE_PLATFORMFILE_BEGIN_OPEN(Path)
#define TRACE_PLATFORMFILE_END_OPEN(FileHandle)
#define TRACE_PLATFORMFILE_BEGIN_CLOSE(FileHandle)
#define TRACE_PLATFORMFILE_END_CLOSE()
#define TRACE_PLATFORMFILE_BEGIN_READ(ReadHandle, FileHandle, Offset, Size)
#define TRACE_PLATFORMFILE_END_READ(ReadHandle, SizeRead)
#define TRACE_PLATFORMFILE_BEGIN_WRITE(WriteHandle, FileHandle, Offset, Size)
#define TRACE_PLATFORMFILE_END_WRITE(WriteHandle, SizeWritten)

#endif
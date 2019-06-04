// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/PlatformFileTrace.h"

#if PLATFORMFILETRACE_ENABLED

#include "Misc/CString.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"

UE_TRACE_EVENT_BEGIN(PlatformFile, BeginOpen, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, TempHandle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, EndOpen, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, TempHandle)
	UE_TRACE_EVENT_FIELD(uint64, FileHandle)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, BeginClose, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, TempHandle)
	UE_TRACE_EVENT_FIELD(uint64, FileHandle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, EndClose, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, TempHandle)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, BeginRead, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ReadHandle)
	UE_TRACE_EVENT_FIELD(uint64, FileHandle)
	UE_TRACE_EVENT_FIELD(uint64, Offset)
	UE_TRACE_EVENT_FIELD(uint64, Size)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, EndRead, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ReadHandle)
	UE_TRACE_EVENT_FIELD(uint64, SizeRead)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, BeginWrite, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WriteHandle)
	UE_TRACE_EVENT_FIELD(uint64, FileHandle)
	UE_TRACE_EVENT_FIELD(uint64, Offset)
	UE_TRACE_EVENT_FIELD(uint64, Size)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, EndWrite, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WriteHandle)
	UE_TRACE_EVENT_FIELD(uint64, SizeWritten)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

void FPlatformFileTrace::BeginOpen(uint64 TempHandle, const TCHAR* Path)
{
	uint16 PathSize = (FCString::Strlen(Path) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(PlatformFile, BeginOpen, PathSize)
		<< BeginOpen.Cycle(FPlatformTime::Cycles64())
		<< BeginOpen.TempHandle(TempHandle)
		<< BeginOpen.Attachment(Path, PathSize)
		<< BeginOpen.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::EndOpen(uint64 TempHandle, uint64 FileHandle)
{
	UE_TRACE_LOG(PlatformFile, EndOpen)
		<< EndOpen.Cycle(FPlatformTime::Cycles64())
		<< EndOpen.TempHandle(TempHandle)
		<< EndOpen.FileHandle(FileHandle);
}

void FPlatformFileTrace::BeginClose(uint64 TempHandle, uint64 FileHandle)
{
	UE_TRACE_LOG(PlatformFile, BeginClose)
		<< BeginClose.Cycle(FPlatformTime::Cycles64())
		<< BeginClose.TempHandle(TempHandle)
		<< BeginClose.FileHandle(FileHandle)
		<< BeginClose.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::EndClose(uint64 TempHandle)
{
	UE_TRACE_LOG(PlatformFile, EndClose)
		<< EndClose.Cycle(FPlatformTime::Cycles64())
		<< EndClose.TempHandle(TempHandle);
}

void FPlatformFileTrace::BeginRead(uint64 ReadHandle, uint64 FileHandle, uint64 Offset, uint64 Size)
{
	UE_TRACE_LOG(PlatformFile, BeginRead)
		<< BeginRead.Cycle(FPlatformTime::Cycles64())
		<< BeginRead.ReadHandle(ReadHandle)
		<< BeginRead.FileHandle(FileHandle)
		<< BeginRead.Offset(Offset)
		<< BeginRead.Size(Size)
		<< BeginRead.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::EndRead(uint64 ReadHandle, uint64 SizeRead)
{
	UE_TRACE_LOG(PlatformFile, EndRead)
		<< EndRead.Cycle(FPlatformTime::Cycles64())
		<< EndRead.ReadHandle(ReadHandle)
		<< EndRead.SizeRead(SizeRead)
		<< EndRead.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::BeginWrite(uint64 WriteHandle, uint64 FileHandle, uint64 Offset, uint64 Size)
{
	UE_TRACE_LOG(PlatformFile, BeginWrite)
		<< BeginWrite.Cycle(FPlatformTime::Cycles64())
		<< BeginWrite.WriteHandle(WriteHandle)
		<< BeginWrite.FileHandle(FileHandle)
		<< BeginWrite.Offset(Offset)
		<< BeginWrite.Size(Size)
		<< BeginWrite.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::EndWrite(uint64 WriteHandle, uint64 SizeWritten)
{
	UE_TRACE_LOG(PlatformFile, EndWrite)
		<< EndWrite.Cycle(FPlatformTime::Cycles64())
		<< EndWrite.WriteHandle(WriteHandle)
		<< EndWrite.SizeWritten(SizeWritten)
		<< EndWrite.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

#endif
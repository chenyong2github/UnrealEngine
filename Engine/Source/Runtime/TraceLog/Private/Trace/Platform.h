// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Detail/Trace.h"

#if UE_TRACE_ENABLED

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
uint8*	MemoryReserve(SIZE_T Size);
void	MemoryFree(void* Address, SIZE_T Size);
void	MemoryMap(void* Address, SIZE_T Size);
void	MemoryUnmap(void* Address, SIZE_T Size);

////////////////////////////////////////////////////////////////////////////////
UPTRINT	ThreadCreate(const ANSICHAR* Name, void (*Entry)());
void	ThreadSleep(uint32 Milliseconds);
void	ThreadJoin(UPTRINT Handle);
void	ThreadDestroy(UPTRINT Handle);

////////////////////////////////////////////////////////////////////////////////
uint64	TimeGetFrequency();
uint64	TimeGetTimestamp();

////////////////////////////////////////////////////////////////////////////////
UPTRINT	TcpSocketConnect(const ANSICHAR* Host, uint16 Port);
UPTRINT	TcpSocketListen(uint16 Port);
int32	TcpSocketAccept(UPTRINT Socket, UPTRINT& Out);
bool	TcpSocketHasData(UPTRINT Socket);

////////////////////////////////////////////////////////////////////////////////
int32	IoRead(UPTRINT Handle, void* Data, uint32 Size);
bool	IoWrite(UPTRINT Handle, const void* Data, uint32 Size);
void	IoClose(UPTRINT Handle);

////////////////////////////////////////////////////////////////////////////////
UPTRINT	FileOpen(const ANSICHAR* Path);

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED

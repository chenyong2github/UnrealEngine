// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MTLTTrace.cpp: Metal device RHI implementation.
 =============================================================================*/

#include "MTITrace.hpp"
#import <Foundation/Foundation.h>

MTLPP_BEGIN

std::fstream& operator>>(std::fstream& fs, MTITraceCommand& dt)
{
	fs >> dt.Class;
	fs >> dt.Thread;
	fs >> dt.Receiver;
	fs >> dt.Cmd;
	return fs;
}

std::fstream& operator<<(std::fstream& fs, const MTITraceCommand& dt)
{
	fs << dt.Class;
	fs << dt.Thread;
	fs << dt.Receiver;
	fs << dt.Cmd;
	return fs;
}

MTITraceCommandHandler::MTITraceCommandHandler(std::string InClass, std::string InCmd)
: Class(InClass)
, Cmd(InCmd)
{
	Id = Class + Cmd;
	MTITrace::Get().RegisterCommandHandler(this);
}

void MTITraceCommandHandler::Trace(std::fstream& fs, uintptr_t Receiver)
{
	fs << Class;
	fs << (uint32)pthread_mach_thread_np(pthread_self());
	fs << Receiver;
	fs << Cmd;
}

MTITrace::MTITrace()
{
	Path = [[NSTemporaryDirectory() stringByAppendingPathComponent:[[NSUUID UUID] UUIDString]] UTF8String];
	File.open(Path, std::ios_base::out|std::ios_base::binary);
	assert(File.is_open());
}

MTITrace::~MTITrace()
{
	if(File.is_open())
	{
		File.flush();
		File.close();
	}
}

MTITrace& MTITrace::Get()
{
	static MTITrace sSelf;
	return sSelf;
}

std::fstream& MTITrace::BeginWrite()
{
	Mutex.lock();
	return File;
}

void MTITrace::EndWrite()
{
	Mutex.unlock();
}

void MTITrace::RegisterObject(uintptr_t Original, id Actual)
{
	Mutex.lock();
	Objects[Original] = Actual;
	Mutex.unlock();
}

id MTITrace::FetchObject(uintptr_t Original)
{
	id Object = nullptr;
	Mutex.lock();
	auto It = Objects.find(Original);
	if (It != Objects.end())
	{
		Object = It->second;
	}
	Mutex.unlock();
	return Object;
}

void MTITrace::RegisterCommandHandler(MTITraceCommandHandler* Handler)
{
	Mutex.lock();
	CommandHandlers[Handler->Id] = Handler;
	Mutex.unlock();
}

MTLPP_END

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/StoreService.h"
#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

#include "AsioContext.h"
#include "AsioRecorder.h"
#include "AsioStore.h"
#include "AsioStoreCborServer.h"
#include "HAL/PlatformFile.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
struct FStoreServiceImpl
{
public:
							FStoreServiceImpl(const FStoreService::FDesc& Desc);
							~FStoreServiceImpl();
	FAsioContext			Context;
	FAsioStore				Store;
	FAsioRecorder			Recorder;
	FAsioStoreCborServer	CborServer;
};

////////////////////////////////////////////////////////////////////////////////
FStoreServiceImpl::FStoreServiceImpl(const FStoreService::FDesc& Desc)
: Context(Desc.ThreadCount)
, Store(Context.Get(), Desc.StoreDir)
, Recorder(Context.Get(), Store)
, CborServer(Context.Get(), Store, Recorder)
{
}

////////////////////////////////////////////////////////////////////////////////
FStoreServiceImpl::~FStoreServiceImpl()
{
	Context.Stop();
}

} // namespace Trace

#endif // TRACE_WITH_ASIO



namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FStoreService* FStoreService::Create(const FDesc& InDesc)
{
#if TRACE_WITH_ASIO
	FDesc Desc = InDesc;

	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
	PlatformFile.CreateDirectory(Desc.StoreDir);

	if (Desc.ThreadCount <= 0)
	{
		Desc.ThreadCount = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
	}

	// TODO: not thread safe yet
	Desc.ThreadCount = 1;

	FStoreServiceImpl* Impl = new FStoreServiceImpl(Desc);
	if (Desc.RecorderPort >= 0)
	{
		FAsioRecorder& Recorder = Impl->Recorder;
		Recorder.StartServer(Desc.RecorderPort);
	}

	Impl->Context.Start();

	return (FStoreService*)Impl;
#else
	return nullptr;
#endif // TRACE_WITH_ASIO
}

////////////////////////////////////////////////////////////////////////////////
void FStoreService::operator delete (void* Addr)
{
#if TRACE_WITH_ASIO
	auto* Self = (FStoreServiceImpl*)Addr;
	delete Self;
#endif // TRACE_WITH_ASIO
}

////////////////////////////////////////////////////////////////////////////////
uint32 FStoreService::GetPort() const
{
#if TRACE_WITH_ASIO
	auto* Self = (FStoreServiceImpl*)this;
	return Self->CborServer.GetPort();
#else
	return 0;
#endif // TRACE_WITH_ASIO
}

} // namespace Trace

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/StoreService.h"
#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

#include "AsioContext.h"
#include "AsioRecorder.h"
#include "AsioStore.h"
#include "AsioStoreCborServer.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
struct FStoreService::FImpl
{
public:
							FImpl(const FDesc& Desc);
	FAsioContext			Context;
	FAsioStore				Store;
	FAsioRecorder			Recorder;
	FAsioStoreCborServer	CborServer;
};

////////////////////////////////////////////////////////////////////////////////
FStoreService::FImpl::FImpl(const FDesc& Desc)
: Context(Desc.ThreadCount)
, Store(Context.Get(), Desc.StoreDir)
, Recorder(Context.Get(), Store)
, CborServer(Context.Get(), Store, Recorder)
{
}

} // namespace Trace

#endif // TRACE_WITH_ASIO



namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FStoreService::~FStoreService()
{
#if TRACE_WITH_ASIO
	delete Impl;
#endif
}

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

	FStoreService::FImpl* Impl = new FStoreService::FImpl(Desc);
	if (Desc.RecorderPort >= 0)
	{
		FAsioRecorder& Recorder = Impl->Recorder;
		Recorder.StartServer(Desc.RecorderPort);
	}

	Impl->Context.Start();

	FStoreService* Server = new FStoreService();
	Server->Impl = Impl;
	return Server;
#else
	return nullptr;
#endif // TRACE_WITH_ASIO
}

////////////////////////////////////////////////////////////////////////////////
uint32 FStoreService::GetPort() const
{
#if TRACE_WITH_ASIO
	return Impl->CborServer.GetPort();
#else
	return 0;
#endif // TRACE_WITH_ASIO
}

} // namespace Trace

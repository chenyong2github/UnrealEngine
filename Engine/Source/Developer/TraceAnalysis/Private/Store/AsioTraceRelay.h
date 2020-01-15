// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

#include "AsioIoable.h"
#include "AsioTcpServer.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FAsioTraceRelay
	: public FAsioTcpServer
	, public FAsioIoSink
{
public:
						FAsioTraceRelay(asio::io_context& IoContext, FAsioReadable* InInput);
						~FAsioTraceRelay();
	void				Close();

private:
	virtual bool		OnAccept(asio::ip::tcp::socket& Socket) override;
	virtual void		OnIoComplete(uint32 Id, int32 Size) override;
	enum				{ OpStart, OpRead, OpSend };
	static const uint32	BufferSize = 64 << 10;
	FAsioReadable*		Input;
	FAsioWriteable*		Output = nullptr;
	uint8				Buffer[BufferSize];
};

} // namespace Trace

#endif // TRACE_WITH_ASIO

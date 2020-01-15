// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

#include "AsioIoable.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FAsioSocket
	: public FAsioReadable
	, public FAsioWriteable
{
public:
							FAsioSocket(asio::ip::tcp::socket& InSocket);
	virtual					~FAsioSocket();
	bool					IsOpen() const;
	virtual void			Close() override;
	virtual bool			Read(void* Dest, uint32 Size, FAsioIoSink* Sink, uint32 Id) override;
	virtual bool			ReadSome(void* Dest, uint32 DestSize, FAsioIoSink* Sink, uint32 Id) override;
	virtual bool			Write(const void* Src, uint32 Size, FAsioIoSink* Sink, uint32 Id) override;

private:
	asio::ip::tcp::socket	Socket;
};

} // namespace Trace

#endif // TRACE_WITH_ASIO

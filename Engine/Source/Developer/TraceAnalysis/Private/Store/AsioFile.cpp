// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioFile.h"

#if TRACE_WITH_ASIO

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FAsioFile::FAsioFile(asio::io_context& IoContext, UPTRINT OsHandle)
: Handle(IoContext, HANDLE(OsHandle))
{
}

////////////////////////////////////////////////////////////////////////////////
void FAsioFile::Close()
{
	Handle.close();
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioFile::Write(const void* Src, uint32 Size, FAsioIoSink* Sink, uint32 Id)
{
	if (!SetSink(Sink, Id))
	{
		return false;
	}

	asio::async_write_at(
		Handle,
		Offset,
		asio::buffer(Src, Size),
		[this] (const asio::error_code& ErrorCode, size_t BytesWritten)
		{
			Offset += BytesWritten;
			return OnIoComplete(ErrorCode, BytesWritten);
		}
	);

	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioFile::Read(void* Dest, uint32 Size, FAsioIoSink* Sink, uint32 Id)
{
	return ReadSome(Dest, Size, Sink, Id);
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioFile::ReadSome(void* Dest, uint32 DestSize, FAsioIoSink* Sink, uint32 Id)
{
	if (!SetSink(Sink, Id))
	{
		return false;
	}

	Handle.async_read_some_at(
		Offset,
		asio::buffer(Dest, DestSize),
		[this] (const asio::error_code& ErrorCode, size_t BytesRead)
		{
			Offset += BytesRead;
			return OnIoComplete(ErrorCode, BytesRead);
		}
	);

	return true;
}

////////////////////////////////////////////////////////////////////////////////
FAsioWriteable* FAsioFile::WriteFile(asio::io_context& IoContext, const TCHAR* Path)
{
	HANDLE Handle = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
		CREATE_ALWAYS, FILE_FLAG_OVERLAPPED|FILE_ATTRIBUTE_NORMAL, nullptr);
	if (Handle == INVALID_HANDLE_VALUE)
	{
		return nullptr;
	}

	return new FAsioFile(IoContext, UPTRINT(Handle));
}

////////////////////////////////////////////////////////////////////////////////
FAsioReadable* FAsioFile::ReadFile(asio::io_context& IoContext, const TCHAR* Path)
{
	HANDLE Handle = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED|FILE_ATTRIBUTE_NORMAL, nullptr);
	if (Handle == INVALID_HANDLE_VALUE)
	{
		return nullptr;
	}

	return new FAsioFile(IoContext, UPTRINT(Handle));
}

} // namespace Trace

#endif // TRACE_WITH_ASIO

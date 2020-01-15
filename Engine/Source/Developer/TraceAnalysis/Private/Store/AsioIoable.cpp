// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioIoable.h"

#if TRACE_WITH_ASIO

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
bool FAsioIoable::SetSink(FAsioIoSink* Ptr, uint32 Id)
{
	if (SinkPtr != nullptr)
	{
		return false;
	}

	SinkPtr = Ptr;
	SinkId = Id;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FAsioIoable::OnIoComplete(const asio::error_code& ErrorCode, int32 Size)
{
	if (ErrorCode)
	{
		Size = -1;
	}

	if (SinkPtr == nullptr)
	{
		return;
	}

	FAsioIoSink* Ptr = SinkPtr;
	SinkPtr = nullptr;
	Ptr->OnIoComplete(SinkId, Size);
}

} // namespace Trace

#endif // TRACE_WITH_ASIO

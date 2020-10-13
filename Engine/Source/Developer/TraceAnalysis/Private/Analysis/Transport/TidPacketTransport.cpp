// Copyright Epic Games, Inc. All Rights Reserved.

#include "TidPacketTransport.h"
#include "Algo/BinarySearch.h"
#include "HAL/UnrealMemory.h"
#include "Trace/Detail/Protocol.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
namespace Private
{

TRACELOG_API int32 Decode(const void*, int32, void*, int32);

} // namespace Private



////////////////////////////////////////////////////////////////////////////////
bool FTidPacketTransport::ReadPacket()
{
	struct FPacketBase
	{
		uint16	PacketSize;
		uint16	ThreadId;
	};

	const auto* PacketBase = GetPointer<FPacketBase>();
	if (PacketBase == nullptr)
	{
		return false;
	}

	if (GetPointer<uint8>(PacketBase->PacketSize) == nullptr)
	{
		return false;
	}

	FTransport::Advance(PacketBase->PacketSize);

	uint32 ThreadId = PacketBase->ThreadId & ~0x8000;
	FThreadStream& Thread = FindOrAddThread(ThreadId);

	uint32 DataSize = PacketBase->PacketSize - sizeof(FPacketBase);
	if (PacketBase->ThreadId != ThreadId)
	{
		uint16* DecodedSize = (uint16*)(PacketBase + 1);
		uint8* Dest = Thread.Buffer.Append(*DecodedSize);
		DataSize -= sizeof(*DecodedSize);
		int32 ResultSize = Private::Decode(DecodedSize + 1, DataSize, Dest, *DecodedSize);
		check(int32(*DecodedSize) == ResultSize);
	}
	else
	{
		Thread.Buffer.Append((uint8*)(PacketBase + 1), DataSize);
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////
FTidPacketTransport::FThreadStream& FTidPacketTransport::FindOrAddThread(uint32 ThreadId)
{
	uint32 ThreadCount = Threads.Num();
	for (uint32 i = 0; i < ThreadCount; ++i)
	{
		if (Threads[i].ThreadId == ThreadId)
		{
			return Threads[i];
		}
	}

	FThreadStream Thread;
	Thread.ThreadId = ThreadId;

	// Internal events are sent over tid 0 and they should be processed first. We
	// don't care about the order otherwise.
	if (Thread.ThreadId == ETransportTid::Internal)
	{
		Threads.Insert(Thread, 0);
		return Threads[0];
	}

	Threads.Add(Thread);
	return Threads[ThreadCount];
}

////////////////////////////////////////////////////////////////////////////////
void FTidPacketTransport::Update()
{
	while (ReadPacket());

	Threads.RemoveAll([] (const FThreadStream& Thread)
	{
		return Thread.Buffer.IsEmpty();
	});
}

////////////////////////////////////////////////////////////////////////////////
uint32 FTidPacketTransport::GetThreadCount() const
{
	return uint32(Threads.Num());
}

////////////////////////////////////////////////////////////////////////////////
FStreamReader* FTidPacketTransport::GetThreadStream(uint32 Index)
{
	return &(Threads[Index].Buffer);
}

////////////////////////////////////////////////////////////////////////////////
int32 FTidPacketTransport::GetThreadId(uint32 Index) const
{
	return Threads[Index].ThreadId;
}

} // namespace Trace

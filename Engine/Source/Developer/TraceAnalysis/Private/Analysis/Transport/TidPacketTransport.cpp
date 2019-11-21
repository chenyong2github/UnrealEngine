// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TidPacketTransport.h"
#include "HAL/UnrealMemory.h"

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
		Private::Decode(DecodedSize + 1, DataSize, Dest, *DecodedSize);
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
	for (auto& Thread : Threads)
	{
		if (Thread.ThreadId == ThreadId)
		{
			return Thread;
		}
	}

	FThreadStream Thread;
	Thread.ThreadId = ThreadId;
	Threads.Add(Thread);
	return Threads.Last();
}

////////////////////////////////////////////////////////////////////////////////
void FTidPacketTransport::Update()
{
	while (ReadPacket());
	Threads.RemoveAllSwap([] (const FThreadStream& Thread) { return Thread.Buffer.IsEmpty(); });

	if (bool bEof = Reader->IsEof())
	{
		for (auto& Thread : Threads)
		{
			Thread.Buffer.SetEof();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
FTidPacketTransport::ThreadIter FTidPacketTransport::ReadThreads()
{
	return ThreadIter(0);
}

////////////////////////////////////////////////////////////////////////////////
FStreamReader* FTidPacketTransport::GetNextThread(ThreadIter& Iter)
{
	while (Iter < Threads.Num())
	{
		FThreadStream& Thread = Threads[Iter++];
		if (!Thread.Buffer.IsEmpty())
		{
			return &(Thread.Buffer);
		}
	}

	return nullptr;
}

} // namespace Trace

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PacketTransport.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
struct FPayloadTransport::FPacketNode
{
	FPacketNode*		Next;
	uint32				Cursor;
	uint16				Serial;
	uint16				Size;
	uint8				Data[];
};

////////////////////////////////////////////////////////////////////////////////
FPayloadTransport::~FPayloadTransport()
{
	for (FPacketNode* Root : {ActiveList, PendingList, FreeList})
	{
		for (FPacketNode* Node = Root; Node != nullptr;)
		{
			FPacketNode* Next = Node->Next;
			delete[] Node;
			Node = Next;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void FPayloadTransport::Advance(uint32 BlockSize)
{
	if (ActiveList != nullptr)
	{
		ActiveList->Cursor += BlockSize;
	}
}

////////////////////////////////////////////////////////////////////////////////
const uint8* FPayloadTransport::GetPointerImpl(uint32 BlockSize)
{
	if (ActiveList == nullptr && !GetNextBatch())
	{
		return nullptr;
	}

	uint32 NextCursor = ActiveList->Cursor + BlockSize;
	if (NextCursor > ActiveList->Size)
	{
		FPacketNode* Node = ActiveList;
		ActiveList = ActiveList->Next;
		Node->Next = FreeList;
		FreeList = Node;
		return GetPointerImpl(BlockSize);
	}

	return ActiveList->Data + ActiveList->Cursor;
}

////////////////////////////////////////////////////////////////////////////////
bool FPayloadTransport::GetNextBatch()
{
	struct FPacket
	{
		int16	Serial;
		uint16	Size;
		uint8	Data[];
	};

	int16 LastSerial = -1;
	if (PendingList != nullptr)
	{
		LastSerial = PendingList->Serial;
	}

	while (true)
	{
		const auto* Packet = (const FPacket*)FTransport::GetPointerImpl(sizeof(FPacket));
		if (Packet == nullptr)
		{
			return false;
		}

		// If this new payload is part of the next event batch then we've finished
		// building the current batch. The current batch can be activated.
		if (LastSerial >= Packet->Serial)
		{
			ActiveList = PendingList;
			PendingList = nullptr;
			break;
		}

		if (FTransport::GetPointerImpl(Packet->Size) == nullptr)
		{
			return false;
		}

		FTransport::Advance(Packet->Size);

		FPacketNode* Node;
		if (FreeList != nullptr)
		{
			Node = FreeList;
			FreeList = Node->Next;
		}
		else
		{
			Node = (FPacketNode*)GMalloc->Malloc(sizeof(FPacketNode) + MaxPacketSize);
		}

		Node->Serial = Packet->Serial;
		Node->Cursor = 0;
		Node->Size = uint16(Packet->Size - sizeof(*Packet));
		Node->Next = PendingList;
		FMemory::Memcpy(Node->Data, Packet->Data, Node->Size);

		PendingList = Node;
		LastSerial = Packet->Serial;
	}

	return true;
}

} // namespace Trace

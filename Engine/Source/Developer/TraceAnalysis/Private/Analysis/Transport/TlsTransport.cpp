// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TlsTransport.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FTlsTransport::~FTlsTransport()
{
	for (FPayloadNode* Root : {ActiveList, PendingList, FreeList})
	{
		for (FPayloadNode* Node = Root; Node != nullptr;)
		{
			FPayloadNode* Next = Node->Next;
			delete[] Node;
			Node = Next;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void FTlsTransport::Advance(uint32 BlockSize)
{
	if (ActiveList != nullptr)
	{
		ActiveList->Cursor += BlockSize;
	}
}

////////////////////////////////////////////////////////////////////////////////
const uint8* FTlsTransport::GetPointerImpl(uint32 BlockSize)
{
	if (ActiveList == nullptr && !GetNextBatch())
	{
		return nullptr;
	}

	uint32 NextCursor = ActiveList->Cursor + BlockSize;
	if (NextCursor > ActiveList->Size)
	{
		FPayloadNode* Node = ActiveList;
		ActiveList = ActiveList->Next;
		Node->Next = FreeList;
		FreeList = Node;
		return GetPointerImpl(BlockSize);
	}

	return ActiveList->Data + ActiveList->Cursor;
}

////////////////////////////////////////////////////////////////////////////////
bool FTlsTransport::GetNextBatch()
{
	struct FPayload
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
		const auto* Payload = (const FPayload*)FTransport::GetPointerImpl(sizeof(FPayload));
		if (Payload == nullptr)
		{
			return false;
		}

		// If this new payload is part of the next event batch then we've finished
		// building the current batch. The current batch can be activated.
		if (LastSerial >= Payload->Serial)
		{
			ActiveList = PendingList;
			PendingList = nullptr;
			break;
		}

		if (FTransport::GetPointerImpl(Payload->Size) == nullptr)
		{
			return false;
		}

		FTransport::Advance(Payload->Size);

		FPayloadNode* Node;
		if (FreeList != nullptr)
		{
			Node = FreeList;
			FreeList = Node->Next;
		}
		else
		{
			Node = (FPayloadNode*)GMalloc->Malloc(sizeof(FPayloadNode) + MaxPayloadSize);
		}

		Node->Serial = Payload->Serial;
		Node->Cursor = 0;
		Node->Size = uint16(Payload->Size - sizeof(*Payload));
		Node->Next = PendingList;
		FMemory::Memcpy(Node->Data, Payload->Data, Node->Size);

		PendingList = Node;
		LastSerial = Payload->Serial;
	}

	return true;
}

} // namespace Trace

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Transport.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FTlsTransport
	: public FTransport
{
public:
	virtual					~FTlsTransport();
	virtual void			Advance(uint32 BlockSize) override;
	virtual const uint8*	GetPointerImpl(uint32 BlockSize) override;

private:
	struct FPacketNode
	{
		FPacketNode*		Next;
		uint32				Cursor;
		uint16				Serial;
		uint16				Size;
		uint8				Data[];
	};

	bool					GetNextBatch();
	static const uint32		MaxPacketSize = 8192;
	FPacketNode*			ActiveList = nullptr;
	FPacketNode*			PendingList = nullptr;
	FPacketNode*			FreeList = nullptr;
};

} // namespace Trace

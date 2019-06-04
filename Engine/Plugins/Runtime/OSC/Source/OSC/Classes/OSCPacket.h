// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "OSCTypes.h"

class FOSCStream;
class FOSCPacket
{
public:
	FOSCPacket() = default;
	virtual ~FOSCPacket() = default;
	
	/** Write packet data into stream */
	virtual void WriteData(FOSCStream& Stream) {}
	
	/** Read packet data from stream */
	virtual void ReadData(FOSCStream& Stream) {}
	
	/** Returns true if packet is message */
	virtual bool IsMessage() { return false; }
	
	/** Returns true if packet is bundle */
	virtual bool IsBundle() { return false; }

	/** Create an OSC packet according to the input data. */
	static TSharedPtr <FOSCPacket> CreatePacket(const char* PacketType);
};

// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "OSCTypes.h"
#include "OSCStream.h"
#include "OSCAddress.h"

// Forward Declarations
class FOSCStream;


class IOSCPacket
{
public:
	IOSCPacket();
	virtual ~IOSCPacket();

	/** Write packet data into stream */
	virtual void WriteData(FOSCStream& Stream) = 0;
	
	/** Read packet data from stream */
	virtual void ReadData(FOSCStream& Stream) = 0;
	
	/** Returns true if packet is message */
	virtual bool IsMessage() = 0;
	
	/** Returns true if packet is bundle */
	virtual bool IsBundle() = 0;

	/** Create an OSC packet according to the input data. */
	static TSharedPtr<IOSCPacket> CreatePacket(const uint8* PacketType);
};

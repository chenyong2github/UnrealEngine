// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "OSCTypes.h"
#include "OSCStream.h"
#include "OSCAddress.h"

// Forward Declarations
class FOSCStream;


class IOSCPacket
{
public:
	IOSCPacket() = default;
	virtual ~IOSCPacket() = default;

	/** Write packet data into stream */
	virtual void WriteData(FOSCStream& OutStream) = 0;
	
	/** Read packet data from stream */
	virtual void ReadData(FOSCStream& OutStream) = 0;
	
	/** Returns true if packet is message */
	virtual bool IsMessage() = 0;
	
	/** Returns true if packet is bundle */
	virtual bool IsBundle() = 0;

	/** Get endpoint responsible for creation/forwarding of packet */
	virtual const FIPv4Endpoint& GetEndpoint() const;

	/** Create an OSC packet according to the input data. */
	static TSharedPtr<IOSCPacket> CreatePacket(const uint8* InPacketType, const FIPv4Endpoint& InEndpoint);

protected:
	FIPv4Endpoint Endpoint;
};

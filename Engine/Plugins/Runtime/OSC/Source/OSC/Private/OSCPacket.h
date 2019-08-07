// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "OSCTypes.h"
#include "OSCStream.h"
#include "OSCAddress.h"

// Forward Declarations
class FOSCStream;


class FOSCPacket
{
public:
	FOSCPacket();
	virtual ~FOSCPacket();

	/** Write packet data into stream */
	virtual void WriteData(FOSCStream& Stream) = 0;
	
	/** Read packet data from stream */
	virtual void ReadData(FOSCStream& Stream) = 0;
	
	/** Returns OSC address identifier of packet */
	virtual const FOSCAddress& GetAddress() const = 0;

	/** Returns true if packet is message */
	virtual bool IsMessage() = 0;
	
	/** Returns true if packet is bundle */
	virtual bool IsBundle() = 0;

	/** Create an OSC packet according to the input data. */
	static TSharedPtr<FOSCPacket> CreatePacket(const uint8* PacketType);
};

class FOSCBundlePacket : public FOSCPacket
{
public:
	FOSCBundlePacket();
	virtual ~FOSCBundlePacket();

	using FPacketBundle = TArray<TSharedPtr<FOSCPacket>>;

	/** Set the bundle time tag. */
	void SetTimeTag(uint64 NewTimeTag);

	/** Get the bundle time tag. */
	uint64 GetTimeTag() const;

	/** Get OSC packet by index. */
	FPacketBundle& GetPackets();

	virtual bool IsBundle() override;
	virtual bool IsMessage() override;

	/** Writes bundle data into the OSC stream. */
	virtual void WriteData(FOSCStream& Stream) override;

	/** Reads bundle data from provided OSC stream,
	  * adding packet data to internal packet bundle. */
	virtual void ReadData(FOSCStream& Stream) override;

	virtual const FOSCAddress& GetAddress() const override;

private:
	/** Bundle of OSC packets. */
	FPacketBundle Packets;

	/** Bundle time tag. */
	FOSCType TimeTag;
};

class FOSCMessagePacket : public FOSCPacket
{
public:
	FOSCMessagePacket();
	virtual ~FOSCMessagePacket();

	/** Set OSC message address. */
	void SetAddress(const FOSCAddress& InAddress);

	/** Get OSC message address. */
	virtual const FOSCAddress& GetAddress() const override;

	/** Get arguments array. */
	TArray<FOSCType>& GetArguments();

	/** Returns false to indicate type is not OSC bundle. */
	virtual bool IsBundle();

	/** Returns true to indicate its an OSC message. */
	virtual bool IsMessage();

	/** Write message data into an OSC stream. */
	virtual void WriteData(FOSCStream& Stream) override;

	/** Reads message data from an OSC stream and creates new argument. */
	virtual void ReadData(FOSCStream& Stream) override;

private:
	/** OSC address. */
	FOSCAddress Address;

	/** List of argument types. */
	TArray<FOSCType> Arguments;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Packets/DMXProtocolE131PDUPacket.h"
#include "DMXProtocolTypes.h"
#include "Serialization/DMXMemoryWriter.h"

#include "DMXProtocolMacros.h"

REGISTER_DMX_ARCHIVE(FDMXProtocolE131RootLayerPacket);
REGISTER_DMX_ARCHIVE(FDMXProtocolE131FramingLayerPacket);
REGISTER_DMX_ARCHIVE(FDMXProtocolE131DMPLayerPacket);
REGISTER_DMX_ARCHIVE(FDMXProtocolUDPE131FramingLayerPacket);
REGISTER_DMX_ARCHIVE(FDMXProtocolUDPE131DiscoveryLayerPacket);

TSharedPtr<FBufferArchive> FDMXProtocolE131RootLayerPacket::Pack()
{
	TSharedPtr<FDMXMemoryWriter> Writer = MakeShared<FDMXMemoryWriter>();
	*Writer << *this;

	return Writer;
}

void FDMXProtocolE131RootLayerPacket::Serialize(FArchive & Ar)
{
	Ar.SetByteSwapping(true);
	Ar << PreambleSize;
	Ar << PostambleSize;
	Ar.SetByteSwapping(false);
	Ar.Serialize((void*)ACNPacketIdentifier, ACN_IDENTIFIER_SIZE);
	Ar.SetByteSwapping(true);
	Ar << Flags;
	Ar << Length;
	Ar << Vector;
	Ar.SetByteSwapping(false);
	Ar.Serialize((void*)CID, ACN_CIDBYTES);
}


TSharedPtr<FBufferArchive> FDMXProtocolE131FramingLayerPacket::Pack()
{
	TSharedPtr<FDMXMemoryWriter> Writer = MakeShared<FDMXMemoryWriter>();
	*Writer << *this;

	return Writer;
}

void FDMXProtocolE131FramingLayerPacket::Serialize(FArchive & Ar)
{
	Ar.SetByteSwapping(true);
	Ar << Flags;
	Ar << Length;
	Ar << Vector;
	Ar.SetByteSwapping(false);
	Ar.Serialize((void*)SourceName, ACN_SOURCE_NAME_SIZE);
	Ar.SetByteSwapping(true);
	Ar << Priority;
	Ar << SynchronizationAddress;
	Ar << SequenceNumber;
	Ar << Options;
	Ar << Universe;
}


TSharedPtr<FBufferArchive> FDMXProtocolE131DMPLayerPacket::Pack()
{
	TSharedPtr<FDMXMemoryWriter> Writer = MakeShared<FDMXMemoryWriter>();
	*Writer << *this;

	return Writer;
}

void FDMXProtocolE131DMPLayerPacket::Serialize(FArchive & Ar)
{
	Ar.SetByteSwapping(true);
	Ar << Flags;
	Ar << Length;
	Ar << Vector;
	Ar << AddressTypeAndDataType;
	Ar << FirstPropertyAddress;
	Ar << AddressIncrement;
	Ar << PropertyValueCount;
	Ar << STARTCode;
	Ar.SetByteSwapping(false);
	Ar.Serialize((void*)DMX, ACN_DMX_SIZE);
}


TSharedPtr<FBufferArchive> FDMXProtocolUDPE131FramingLayerPacket::Pack()
{
	TSharedPtr<FDMXMemoryWriter> Writer = MakeShared<FDMXMemoryWriter>();
	*Writer << *this;

	return Writer;
}

void FDMXProtocolUDPE131FramingLayerPacket::Serialize(FArchive & Ar)
{
	Ar.SetByteSwapping(true);
	Ar << Flags;
	Ar << Length;
	Ar << Vector;
	Ar.SetByteSwapping(false);
	Ar.Serialize((void*)SourceName, ACN_SOURCE_NAME_SIZE);
	Ar.Serialize((void*)Reserved, sizeof(Reserved));
}

TSharedPtr<FBufferArchive> FDMXProtocolUDPE131DiscoveryLayerPacket::Pack()
{
	TSharedPtr<FDMXMemoryWriter> Writer = MakeShared<FDMXMemoryWriter>();
	*Writer << *this;

	return Writer;
}

void FDMXProtocolUDPE131DiscoveryLayerPacket::Serialize(FArchive & Ar)
{
	Ar.SetByteSwapping(true);
	Ar << Flags;
	Ar << Length;
	Ar << Vector;
	Ar << Page;
	Ar << Last;
	Ar.SetByteSwapping(false);
	Ar.Serialize((void*)Universes, sizeof(Universes));
}

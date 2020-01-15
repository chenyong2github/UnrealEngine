// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IDMXProtocolPacket.h"
#include "DMXProtocolSACNConstants.h"

struct FDMXProtocolE131RootLayerPacket
	: public IDMXProtocolPacket
{
public:
	//~ Begin IDMXProtocolPacket implementation
	virtual TSharedPtr<FBufferArchive> Pack() override;
	//~ End IDMXProtocolPort implementation

	void Serialize(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FDMXProtocolE131RootLayerPacket& Packet);

public:
	uint16 PreambleSize = ACN_RLP_PREAMBLE_SIZE;
	uint16 PostambleSize = 0;
	uint8 ACNPacketIdentifier[ACN_IDENTIFIER_SIZE] = { 'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', '\0', '\0', '\0' };
	uint8 Flags = 0x72;
	uint8 Length = 0x6e;
	uint32 Vector = VECTOR_ROOT_E131_DATA;
	uint8 CID[ACN_CIDBYTES] = { 0 };
};

struct FDMXProtocolE131FramingLayerPacket
	: public IDMXProtocolPacket
{
public:
	//~ Begin IDMXProtocolPacket implementation
	virtual TSharedPtr<FBufferArchive> Pack() override;
	//~ End IDMXProtocolPort implementation

	void Serialize(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FDMXProtocolE131FramingLayerPacket& Packet);

public:
	uint8 Flags = 0;
	uint8 Length = 0;
	uint32 Vector = VECTOR_E131_DATA_PACKET;
	uint8 SourceName[ACN_SOURCE_NAME_SIZE] = "New Source";
	uint8 Priority = 0;
	uint16 SynchronizationAddress = 0;
	uint8 SequenceNumber = 0;
	uint8 Options = 0;
	uint8 Universe = 0;
};

struct FDMXProtocolE131DMPLayerPacket
	: public IDMXProtocolPacket
{
public:
	//~ Begin IDMXProtocolPacket implementation
	virtual TSharedPtr<FBufferArchive> Pack() override;
	//~ End IDMXProtocolPort implementation

	void Serialize(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FDMXProtocolE131DMPLayerPacket& Packet);

public:
	uint8 Flags = 0;
	uint8 Length = 0;
	uint8 Vector = VECTOR_DMP_SET_PROPERTY;
	uint8 AddressTypeAndDataType = 0;
	uint16 FirstPropertyAddress = 0;
	uint16 AddressIncrement = 0;
	uint16 PropertyValueCount = 0;
	uint8 STARTCode = 0;
	uint8 DMX[ACN_DMX_SIZE] = { 0 };
};

struct FDMXProtocolUDPE131FramingLayerPacket
	: public IDMXProtocolPacket
{
public:
	//~ Begin IDMXProtocolPacket implementation
	virtual TSharedPtr<FBufferArchive> Pack() override;
	//~ End IDMXProtocolPort implementation

	void Serialize(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FDMXProtocolUDPE131FramingLayerPacket& Packet);

public:
	uint8 Flags = 0x72;
	uint8 Length = 0x0b;
	uint32 Vector = 2;
	uint8 SourceName[ACN_SOURCE_NAME_SIZE] = { 0 };
	uint8 Reserved[4] = { 0 };

};

struct FDMXProtocolUDPE131DiscoveryLayerPacket
	: public IDMXProtocolPacket
{
public:
	//~ Begin IDMXProtocolPacket implementation
	virtual TSharedPtr<FBufferArchive> Pack() override;
	//~ End IDMXProtocolPort implementation

	void Serialize(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FDMXProtocolUDPE131DiscoveryLayerPacket& Packet);

public:
	uint8 Flags = 0;
	uint8 Length = 0;
	uint32 Vector = VECTOR_E131_EXTENDED_DISCOVERY;
	uint8 Page = 0;
	uint8 Last = 0;
	uint16 Universes[ACN_DMX_SIZE] = { 0 };

};


// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolPackager.h"
#include "Interfaces/IDMXProtocolPacket.h"
#include "Serialization/BufferArchive.h"

bool FDMXProtocolPackager::AddToPackage(IDMXProtocolPacket* Packet)
{
	if (Packet != nullptr)
	{
		Buffer.Append(*Packet->Pack());
		return true;
	}

	return false;
}

uint32 FDMXProtocolPackager::GetSize() const
{
	return Buffer.Num();
}

const TArray<uint8>& FDMXProtocolPackager::GetBuffer() const
{
	return Buffer;
}

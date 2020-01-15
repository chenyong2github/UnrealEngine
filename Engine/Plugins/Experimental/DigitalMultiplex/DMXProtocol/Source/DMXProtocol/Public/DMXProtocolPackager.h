// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

class DMXPROTOCOL_API FDMXProtocolPackager
{
public:
	bool AddToPackage(IDMXProtocolPacket* Packet);
	uint32 GetSize() const;
	const TArray<uint8>& GetBuffer() const;

private:
	TArray<uint8> Buffer;
};


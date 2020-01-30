// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolCommon.h"
#include "DMXProtocolConstants.h"
#include "Dom/JsonObject.h"

struct DMXPROTOCOL_API FDMXBuffer
{
public:
	FDMXBuffer()
	{
		DMXData.AddZeroed(DMX_UNIVERSE_SIZE);
	}

	bool SetDMXFragment(const IDMXFragmentMap & DMXFragment);
	TArray<uint8>& GetDMXData() { return DMXData; }

private:
	TArray<uint8> DMXData;
};

struct DMXPROTOCOL_API FDMXPacket
{
	FDMXPacket(FJsonObject& InSettings, const TArray<uint8>& InData)
	{
		Settings = InSettings;
		Data = InData;
	}

	FDMXPacket(const TArray<uint8>& InData)
	{
		Data = InData;
	}

	FJsonObject Settings;
	TArray<uint8> Data;
};

struct DMXPROTOCOL_API FRDMUID
{
	FRDMUID()
	{
	}

	FRDMUID(uint8 InBuffer[RDM_UID_WIDTH])
	{
		FMemory::Memcpy(Buffer, InBuffer, RDM_UID_WIDTH);
	}

	FRDMUID(const TArray<uint8>& InBuffer)
	{
		// Only copy if we have the requested amount of data
		if (InBuffer.Num() == RDM_UID_WIDTH)
		{
			FMemory::Memmove(Buffer, InBuffer.GetData(), RDM_UID_WIDTH);
		}
		else
		{
			UE_LOG_DMXPROTOCOL(Warning, TEXT("Size of the TArray buffer is wrong"));
		}
	}

	uint8 Buffer[RDM_UID_WIDTH] = { 0 };
};



// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

DMXPROTOCOL_API DECLARE_LOG_CATEGORY_EXTERN(LogDMXProtocol, Log, All);

DECLARE_STATS_GROUP(TEXT("DMXProtocol"), STATGROUP_DMXProtocol, STATCAT_Advanced);

#ifndef DMXPROTOCOL_LOG_PREFIX
#define DMXPROTOCOL_LOG_PREFIX TEXT("DMX: ")
#endif

#define UE_LOG_DMXPROTOCOL(Verbosity, Format, ...) \
{ \
	UE_LOG(LogDMXProtocol, Verbosity, TEXT("%s%s"), DMXPROTOCOL_LOG_PREFIX, *FString::Printf(Format, ##__VA_ARGS__)); \
}

#define UE_CLOG_DMXPROTOCOL(Conditional, Verbosity, Format, ...) \
{ \
	UE_CLOG(Conditional, LogDMXProtocol, Verbosity, TEXT("%s%s"), DMXPROTOCOL_LOG_PREFIX, *FString::Printf(Format, ##__VA_ARGS__)); \
}

#define REGISTER_DMX_ARCHIVE(ProtocolDMXPacket) \
	FArchive & operator<<(FArchive & Ar, ProtocolDMXPacket&Packet) \
	{ \
		Packet.Serialize(Ar); \
		return Ar; \
	}
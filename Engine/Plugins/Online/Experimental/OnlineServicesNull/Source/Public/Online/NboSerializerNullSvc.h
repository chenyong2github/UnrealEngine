// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/NboSerializer.h"

#include "Online/AuthNull.h"
#include "Online/LobbiesNull.h"
#include "Online/SessionsLAN.h"
#include "Online/CoreOnline.h"

/**
 * Serializes data in network byte order form into a buffer
 */
namespace UE::Online {

namespace NboSerializerNullSvc {

/** NboSerializeToBuffer methods */

inline void SerializeToBuffer(FNboSerializeToBuffer& Ar, const FOnlineAccountIdHandle& UniqueId)
{
	TArray<uint8> Data = FOnlineAccountIdRegistryNull::Get().ToReplicationData(UniqueId);
	Ar << Data.Num();
	Ar.WriteBinary(Data.GetData(), Data.Num());
}

inline void SerializeToBuffer(FNboSerializeToBuffer& Ar, const FOnlineSessionIdHandle& SessionId)
{
	TArray<uint8> Data = FOnlineSessionIdRegistryLAN::Get().ToReplicationData(SessionId);
	Ar << Data.Num();
	Ar.WriteBinary(Data.GetData(), Data.Num());
}

inline void SerializeToBuffer(FNboSerializeToBuffer& Ar, const TMap<FLobbyAttributeId, FLobbyVariant>& Map)
{
	Ar << Map.Num();
	for (const TPair<FLobbyAttributeId, FLobbyVariant>& Pair : Map)
	{
		Ar << Pair.Key;
		Ar << Pair.Value.GetString();
	}
}

/** NboSerializeFromBuffer methods */

inline void SerializeFromBuffer(FNboSerializeFromBuffer& Ar, FOnlineAccountIdHandle& UniqueId)
{
	TArray<uint8> Data;
	int32 Size;
	Ar >> Size;
	Ar.ReadBinaryArray(Data, Size);

	UniqueId = FOnlineAccountIdRegistryNull::Get().FromReplicationData(Data);
}

inline void SerializeFromBuffer(FNboSerializeFromBuffer& Ar, FOnlineSessionIdHandle& SessionId)
{
	TArray<uint8> Data;
	int32 Size;
	Ar >> Size;
	Ar.ReadBinaryArray(Data, Size);

	SessionId = FOnlineSessionIdRegistryLAN::Get().FromReplicationData(Data);
}

inline void SerializeFromBuffer(FNboSerializeFromBuffer& Ar, TMap<FLobbyAttributeId, FLobbyVariant>& Map)
{
	int32 Size;
	Ar >> Size;
	for (int i = 0; i < Size; i++)
	{
		FLobbyAttributeId LobbyAttributeId;
		FString LobbyData;
		Ar >> LobbyAttributeId;
		Ar >> LobbyData;
		Map.Emplace(MoveTemp(LobbyAttributeId), MoveTemp(LobbyData));
	}
}

/* NboSerializerNullSvc */ }

/* UE::Online */ }
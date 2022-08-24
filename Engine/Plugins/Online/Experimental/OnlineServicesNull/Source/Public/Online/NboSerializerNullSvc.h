// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/NboSerializer.h"

#include "Online/AuthNull.h"
#include "Online/LobbiesNull.h"
#include "Online/SessionsNull.h"
#include "Online/CoreOnline.h"
#include "Online/NboSerializerCommonSvc.h"

/**
 * Serializes data in network byte order form into a buffer
 */
namespace UE::Online {

namespace NboSerializerNullSvc {

/** NboSerializeToBuffer methods */

inline void SerializeToBuffer(FNboSerializeToBuffer& Ar, const FAccountId& UniqueId)
{
	TArray<uint8> Data = FOnlineAccountIdRegistryNull::Get().ToReplicationData(UniqueId);
	Ar << Data.Num();
	Ar.WriteBinary(Data.GetData(), Data.Num());
}

inline void SerializeToBuffer(FNboSerializeToBuffer& Ar, const FOnlineSessionIdHandle& SessionId)
{
	TArray<uint8> Data = FOnlineSessionIdRegistryNull::Get().ToReplicationData(SessionId);
	Ar << Data.Num();
	Ar.WriteBinary(Data.GetData(), Data.Num());
}

inline void SerializeToBuffer(FNboSerializeToBuffer& Packet, const FSessionMembersMap& SessionMembersMap)
{
	Packet << SessionMembersMap.Num();

	for (const TPair<FAccountId, FSessionMember>& Entry : SessionMembersMap)
	{
		SerializeToBuffer(Packet, Entry.Key);
		NboSerializerCommonSvc::SerializeToBuffer(Packet, Entry.Value);
	}
}

inline void SerializeToBuffer(FNboSerializeToBuffer& Packet, const FRegisteredPlayersMap& RegisteredPlayersMap)
{
	Packet << RegisteredPlayersMap.Num();

	for (const TPair<FAccountId, FRegisteredPlayer>& Entry : RegisteredPlayersMap)
	{
		SerializeToBuffer(Packet, Entry.Key);
		NboSerializerCommonSvc::SerializeToBuffer(Packet, Entry.Value);
	}
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

inline void SerializeFromBuffer(FNboSerializeFromBuffer& Ar, FAccountId& UniqueId)
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

	SessionId = FOnlineSessionIdRegistryNull::Get().FromReplicationData(Data);
}

inline void SerializeFromBuffer(FNboSerializeFromBuffer& Packet, FSessionMembersMap& SessionMembersMap)
{
	int32 NumEntries = 0;
	Packet >> NumEntries;

	for (int32 Index = 0; Index < NumEntries; ++Index)
	{
		FAccountId Key;
		SerializeFromBuffer(Packet, Key);

		FSessionMember Value;
		NboSerializerCommonSvc::SerializeFromBuffer(Packet, Value);

		SessionMembersMap.Emplace(Key, Value);
	}
}

inline void SerializeFromBuffer(FNboSerializeFromBuffer& Packet, FRegisteredPlayersMap& RegisteredPlayersMap)
{
	int32 NumEntries = 0;
	Packet >> NumEntries;

	for (int32 Index = 0; Index < NumEntries; ++Index)
	{
		FAccountId Key;
		SerializeFromBuffer(Packet, Key);

		FRegisteredPlayer Value;
		NboSerializerCommonSvc::SerializeFromBuffer(Packet, Value);

		RegisteredPlayersMap.Emplace(Key, Value);
	}
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
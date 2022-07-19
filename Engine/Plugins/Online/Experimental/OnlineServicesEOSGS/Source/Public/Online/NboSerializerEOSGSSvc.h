// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/NboSerializer.h"

#include "Online/AuthEOSGS.h"
#include "Online/LobbiesEOSGS.h"
#include "Online/SessionsEOSGS.h"
#include "Online/CoreOnline.h"

/**
 * Serializes data in network byte order form into a buffer
 */
namespace UE::Online {

namespace NboSerializerEOSGSSvc {

/** NboSerializeToBuffer methods */

inline void SerializeToBuffer(FNboSerializeToBuffer& Ar, const FOnlineAccountIdHandle& UniqueId)
{
	TArray<uint8> Data = FOnlineAccountIdRegistryEOSGS::GetRegistered().ToReplicationData(UniqueId);
	Ar << Data.Num();
	Ar.WriteBinary(Data.GetData(), Data.Num());
}

inline void SerializeToBuffer(FNboSerializeToBuffer& Ar, const FOnlineSessionIdHandle& SessionId)
{
	TArray<uint8> Data = FOnlineSessionIdRegistryEOSGS::Get().ToReplicationData(SessionId);
	Ar << Data.Num();
	Ar.WriteBinary(Data.GetData(), Data.Num());
}

/** NboSerializeFromBuffer methods */

inline void SerializeFromBuffer(FNboSerializeFromBuffer& Ar, FOnlineAccountIdHandle& UniqueId)
{
	TArray<uint8> Data;
	int32 Size;
	Ar >> Size;
	Ar.ReadBinaryArray(Data, Size);
	UniqueId = FOnlineAccountIdRegistryEOSGS::GetRegistered().FromReplicationData(Data);
}

inline void SerializeFromBuffer(FNboSerializeFromBuffer& Ar, FOnlineSessionIdHandle& SessionId)
{
	TArray<uint8> Data;
	int32 Size;
	Ar >> Size;
	Ar.ReadBinaryArray(Data, Size);
	SessionId = FOnlineSessionIdRegistryEOSGS::Get().FromReplicationData(Data);
}

/* NboSerializerNullSvc */ }

/* UE::Online */ }
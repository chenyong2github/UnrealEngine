// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NboSerializer.h"
#include "Online/AuthNull.h"
#include "Online/CoreOnline.h"

/**
 * Serializes data in network byte order form into a buffer
 */
 namespace UE::Online {

class FNboSerializeToBufferNullSvc : public FNboSerializeToBuffer
{
public:
	/** Default constructor zeros num bytes*/
	FNboSerializeToBufferNullSvc() :
		FNboSerializeToBuffer(512)
	{
	}

	/** Constructor specifying the size to use */
	FNboSerializeToBufferNullSvc(uint32 Size) :
		FNboSerializeToBuffer(Size)
	{
	}

	/**
	 * Adds Null Id to the buffer
	 */
	friend inline FNboSerializeToBufferNullSvc& operator<<(FNboSerializeToBufferNullSvc& Ar, const FOnlineAccountIdHandle& UniqueId)
	{
		TArray<uint8> Data = FOnlineAccountIdRegistryNull::Get().ToReplicationData(UniqueId);
		Ar << Data.Num();
		Ar.WriteBinary(Data.GetData(), Data.Num());
		return Ar;
	}

	friend inline FNboSerializeToBufferNullSvc& operator<<(FNboSerializeToBufferNullSvc& Ar, const TMap<FLobbyAttributeId, FLobbyVariant>& Map)
	{
		Ar << Map.Num();
		for (const TPair<FLobbyAttributeId, FLobbyVariant>& Pair : Map)
		{
			Ar << Pair.Key;
			Ar << Pair.Value.GetString();
		}
		return Ar;
	}
};

/**
 * Class used to write data into packets for sending via system link
 */
class FNboSerializeFromBufferNullSvc : public FNboSerializeFromBuffer
{
public:
	/**
	 * Initializes the buffer, size, and zeros the read offset
	 */
	FNboSerializeFromBufferNullSvc(uint8* Packet,int32 Length) :
		FNboSerializeFromBuffer(Packet,Length)
	{
	}
	/**
	 * Reads Null Id from the buffer
	 */
	friend inline FNboSerializeFromBufferNullSvc& operator>>(FNboSerializeFromBufferNullSvc& Ar, FOnlineAccountIdHandle& UniqueId)
	{
		TArray<uint8> Data;
		int32 Size;
		Ar >> Size;
		Ar.ReadBinaryArray(Data, Size);
		UniqueId = FOnlineAccountIdRegistryNull::Get().FromReplicationData(Data);
		return Ar;
	}
	
	friend inline FNboSerializeFromBufferNullSvc& operator>>(FNboSerializeFromBufferNullSvc& Ar, TMap<FLobbyAttributeId, FLobbyVariant>& Map)
	{
		int32 Size;
		Ar >> Size;
		for(int i = 0; i < Size; i++)
		{
			FLobbyAttributeId LobbyAttributeId;
			FString LobbyData;
			Ar >> LobbyAttributeId;
			Ar >> LobbyData;
			Map.Emplace(MoveTemp(LobbyAttributeId), MoveTemp(LobbyData));
		}
		return Ar;
	}
};

} //namespace UE::Online
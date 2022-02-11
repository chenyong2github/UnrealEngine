// Copyright Epic Games, Inc. All Rights Reserved.
#include "Online/LobbyRegistryNull.h"

namespace UE::Online {

FOnlineLobbyIdRegistryNull& FOnlineLobbyIdRegistryNull::Get()
{
	static FOnlineLobbyIdRegistryNull Instance;
	return Instance;
}

const FOnlineLobbyIdHandle* FOnlineLobbyIdRegistryNull::Find(FString LobbyId)
{
	return StringToId.Find(LobbyId);
}

FOnlineLobbyIdHandle FOnlineLobbyIdRegistryNull::FindOrAdd(FString LobbyId)
{
	const FOnlineLobbyIdHandle* Entry = StringToId.Find(LobbyId);
	if (Entry)
	{
		return *Entry;
	}

	Ids.Add(LobbyId);
	FOnlineLobbyIdHandle Handle(EOnlineServices::Null, Ids.Num());
	StringToId.Add(LobbyId, Handle);

	return Handle;
}

UE::Online::FOnlineLobbyIdHandle FOnlineLobbyIdRegistryNull::GetNext()
{
	return FindOrAdd(FGuid().ToString());
}


const FString* FOnlineLobbyIdRegistryNull::GetInternal(const FOnlineLobbyIdHandle& Handle) const
{
	if (Handle.GetOnlineServicesType() == EOnlineServices::Null && Handle.GetHandle() <= (uint32)Ids.Num())
	{
		return &Ids[Handle.GetHandle()-1];
	}
	return nullptr;
}

FString FOnlineLobbyIdRegistryNull::ToLogString(const FOnlineLobbyIdHandle& Handle) const
{
	if (const FString* Id = GetInternal(Handle))
	{
		return *Id;
	}

	return FString(TEXT("[InvalidLobbyID]"));
}


TArray<uint8> FOnlineLobbyIdRegistryNull::ToReplicationData(const FOnlineLobbyIdHandle& Handle) const
{
	if (const FString* Id = GetInternal(Handle))
	{
		TArray<uint8> ReplicationData;
		ReplicationData.Reserve(Id->Len());
		StringToBytes(*Id, ReplicationData.GetData(), Id->Len());
		return ReplicationData;
	}

	return TArray<uint8>();;
}

FOnlineLobbyIdHandle FOnlineLobbyIdRegistryNull::FromReplicationData(const TArray<uint8>& ReplicationData)
{
	FString Result = BytesToString(ReplicationData.GetData(), ReplicationData.Num());
	if (Result.Len() > 0)
	{
		return FindOrAdd(Result);
	}
	return FOnlineLobbyIdHandle();
}

} // namespace UE::Online

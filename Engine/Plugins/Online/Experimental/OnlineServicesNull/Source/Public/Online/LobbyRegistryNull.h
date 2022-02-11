// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/LobbiesCommon.h"

namespace UE::Online {
using IOnlineLobbyIdRegistry = IOnlineIdRegistry<OnlineIdHandleTags::FLobby>; // todo: remove this when its added to the global scope
class FOnlineLobbyIdRegistryNull : public IOnlineLobbyIdRegistry
{
public:
	static FOnlineLobbyIdRegistryNull& Get();

	const FOnlineLobbyIdHandle* Find(FString LobbyId);
	FOnlineLobbyIdHandle FindOrAdd(FString LobbyId);
	FOnlineLobbyIdHandle GetNext();

	// Begin IOnlineAccountIdRegistry
	virtual FString ToLogString(const FOnlineLobbyIdHandle& Handle) const override;
	virtual TArray<uint8> ToReplicationData(const FOnlineLobbyIdHandle& Handle) const override;
	virtual FOnlineLobbyIdHandle FromReplicationData(const TArray<uint8>& ReplicationString) override;
	// End IOnlineAccountIdRegistry

	virtual ~FOnlineLobbyIdRegistryNull() = default;

private:
	const FString* GetInternal(const FOnlineLobbyIdHandle& Handle) const;
	TArray<FString> Ids;
	TMap<FString, FOnlineLobbyIdHandle> StringToId; 

};

} // namespace UE::Online
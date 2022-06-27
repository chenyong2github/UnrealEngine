// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "LANBeacon.h"
#include "Online/SessionsCommon.h"

namespace UE::Online {

class FOnlineServicesNull;

class FOnlineSessionIdString
{
public:
	FString Data;
	FOnlineSessionIdHandle Handle;
};

class FOnlineSessionIdRegistryNull : public IOnlineSessionIdRegistry
{
public:
	static FOnlineSessionIdRegistryNull& Get();

	FOnlineSessionIdHandle Find(FString SessionId) const;
	FOnlineSessionIdHandle FindOrAdd(FString SessionId);
	FOnlineSessionIdHandle GetNext();

	// Begin IOnlineSessionIdRegistry
	virtual FString ToLogString(const FOnlineSessionIdHandle& Handle) const override;
	virtual TArray<uint8> ToReplicationData(const FOnlineSessionIdHandle& Handle) const override;
	virtual FOnlineSessionIdHandle FromReplicationData(const TArray<uint8>& ReplicationString) override;
	// End IOnlineSessionIdRegistry

	virtual ~FOnlineSessionIdRegistryNull() = default;

private:
	const FOnlineSessionIdString* GetInternal(const FOnlineSessionIdHandle& Handle) const;

	TArray<FOnlineSessionIdString> Ids;
	TMap<FString, FOnlineSessionIdString> StringToId;
};

class FSessionNull : public FSession
{
public:
	FSessionNull();
	FSessionNull(const FSession& InSession);

public:
	/** The IP address for the session owner */
	TSharedPtr<FInternetAddr> OwnerInternetAddr;
};

class FSessionsNull : public FSessionsCommon
{
public:
	using Super = FSessionsCommon;

	FSessionsNull(FOnlineServicesNull& InServices);
	virtual ~FSessionsNull() = default;

	virtual void Tick(float DeltaSeconds) override;

	// ISessions
	virtual TOnlineResult<FGetAllSessions> GetAllSessions(FGetAllSessions::Params&& Params) override;
	virtual TOnlineResult<FGetSessionByName> GetSessionByName(FGetSessionByName::Params&& Params) override;
	virtual TOnlineResult<FGetSessionById> GetSessionById(FGetSessionById::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FCreateSession> CreateSession(FCreateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUpdateSession> UpdateSession(FUpdateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FFindSessions> FindSessions(FFindSessions::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FJoinSession> JoinSession(FJoinSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FLeaveSession> LeaveSession(FLeaveSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FRegisterPlayers> RegisterPlayers(FRegisterPlayers::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUnregisterPlayers> UnregisterPlayers(FUnregisterPlayers::Params&& Params) override;

private:
	/** LAN Methods */
	bool TryHostLANSession();
	void FindLANSessions();
	void StopLANSession();
	void OnValidQueryPacketReceived(uint8* PacketData, int32 PacketLength, uint64 ClientNonce);
	void OnValidResponsePacketReceived(uint8* PacketData, int32 PacketLength);
	void OnLANSearchTimeout();
	void AppendSessionToPacket(class FNboSerializeToBufferNullSvc& Packet, const TSharedRef<FSessionNull>& Session);
	void ReadSessionFromPacket(class FNboSerializeFromBufferNullSvc& Packet, const TSharedRef<FSessionNull>& Session);
	
private:
	FOnlineServicesNull& Services;

	TMap<FName, TSharedRef<FSessionNull>> SessionsByName;
	TMap<FOnlineSessionIdHandle, TSharedRef<FSessionNull>> SessionsById;

	TSharedPtr<FFindSessions::Result> CurrentSessionSearch;
	TSharedPtr<TOnlineAsyncOp<FFindSessions>> CurrentSessionSearchHandle;

	uint32 PublicSessionsHosted = 0;

	TSharedPtr<FLANSession> LANSessionManager;
};

/* UE::Online */ }
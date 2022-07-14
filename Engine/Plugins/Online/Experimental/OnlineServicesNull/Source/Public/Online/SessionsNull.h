// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Online/LANBeacon.h"
#include "Online/SessionsCommon.h"

namespace UE::Online {

class FOnlineServicesNull;

class FOnlineSessionIdRegistryNull : public TOnlineSessionIdStringRegistry<EOnlineServices::Null>
{
public:
	static FOnlineSessionIdRegistryNull& Get();

	FOnlineSessionIdHandle GetNextSessionId();
};

class FSessionNull : public FSession
{
public:
	FSessionNull();
	FSessionNull(const FSession& InSession);

	static FSessionNull& Cast(FSession& InSession);
	static const FSessionNull& Cast(const FSession& InSession);

private:
	void Initialize();

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
	virtual TOnlineAsyncOpHandle<FCreateSession> CreateSession(FCreateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUpdateSession> UpdateSession(FUpdateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FFindSessions> FindSessions(FFindSessions::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FJoinSession> JoinSession(FJoinSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FLeaveSession> LeaveSession(FLeaveSession::Params&& Params) override;

private:
	/** LAN Methods */
	bool TryHostLANSession();
	void FindLANSessions();
	void StopLANSession();
	void OnValidQueryPacketReceived(uint8* PacketData, int32 PacketLength, uint64 ClientNonce);
	void OnValidResponsePacketReceived(uint8* PacketData, int32 PacketLength);
	void OnLANSearchTimeout();
	void AppendSessionToPacket(class FNboSerializeToBufferNullSvc& Packet, const FSessionNull& Session);
	void ReadSessionFromPacket(class FNboSerializeFromBufferNullSvc& Packet, FSessionNull& Session);
	
private:
	uint32 PublicSessionsHosted = 0;

	TSharedPtr<FLANSession> LANSessionManager;
};

/* UE::Online */ }
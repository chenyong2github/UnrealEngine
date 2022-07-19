// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Online/LANBeacon.h"
#include "Online/NboSerializer.h"
#include "Online/SessionsCommon.h"

namespace UE::Online {

class FOnlineServicesCommon;

class ONLINESERVICESCOMMONENGINEUTILS_API FOnlineSessionIdRegistryLAN : public TOnlineSessionIdStringRegistry<EOnlineServices::Default>
{
public:
	static FOnlineSessionIdRegistryLAN& Get();

	FOnlineSessionIdHandle GetNextSessionId();
};

class ONLINESERVICESCOMMONENGINEUTILS_API FSessionLAN : public FSession
{
public:
	FSessionLAN();
	FSessionLAN(const FSessionLAN& InSession) = default;

	static FSessionLAN& Cast(FSession& InSession);
	static const FSessionLAN& Cast(const FSession& InSession);

private:
	void Initialize();

public:
	/** The IP address for the session owner */
	TSharedPtr<FInternetAddr> OwnerInternetAddr;
};

class ONLINESERVICESCOMMONENGINEUTILS_API FSessionsLAN : public FSessionsCommon
{
public:
	using Super = FSessionsCommon;

	FSessionsLAN(FOnlineServicesCommon& InServices);
	virtual ~FSessionsLAN() = default;

	virtual void Tick(float DeltaSeconds) override;

	// ISessions
	virtual TOnlineAsyncOpHandle<FCreateSession> CreateSession(FCreateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUpdateSession> UpdateSession(FUpdateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FFindSessions> FindSessions(FFindSessions::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FJoinSession> JoinSession(FJoinSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FLeaveSession> LeaveSession(FLeaveSession::Params&& Params) override;

protected:
	TOptional<FOnlineError> TryHostLANSession();
	void FindLANSessions();
	void StopLANSession();
	void OnValidQueryPacketReceived(uint8* PacketData, int32 PacketLength, uint64 ClientNonce);
	void OnValidResponsePacketReceived(uint8* PacketData, int32 PacketLength);
	void OnLANSearchTimeout();
	virtual void AppendSessionToPacket(FNboSerializeToBuffer& Packet, const FSessionLAN& Session) = 0;
	virtual void ReadSessionFromPacket(FNboSerializeFromBuffer& Packet, FSessionLAN& Session) = 0;

protected:
	uint32 PublicSessionsHosted = 0;

	TSharedRef<FLANSession> LANSessionManager;
};

/* UE::Online */ }
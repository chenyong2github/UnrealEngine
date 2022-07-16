// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Online/LANBeacon.h"
#include "Online/SessionsLAN.h"

namespace UE::Online {

class FOnlineServicesNull;

class FOnlineSessionIdRegistryNull : public TOnlineSessionIdStringRegistry<EOnlineServices::Null>
{
public:
	static FOnlineSessionIdRegistryNull& Get();

	FOnlineSessionIdHandle GetNextSessionId();
};

typedef FSessionLAN FSessionNull;

class FSessionsNull : public FSessionsLAN
{
public:
	using Super = FSessionsLAN;

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
	// FSessionsLAN
	virtual void AppendSessionToPacket(FNboSerializeToBuffer& Packet, const FSessionLAN& Session) override;
	virtual void ReadSessionFromPacket(FNboSerializeFromBuffer& Packet, FSessionLAN& Session) override;
};

/* UE::Online */ }
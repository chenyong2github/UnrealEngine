// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/Sessions.h"
#include "Online/OnlineComponent.h"
#include "Online/OnlineIdCommon.h"

namespace UE::Online {

class FOnlineServicesCommon;

template<EOnlineServices OnlineServicesType>
class TOnlineSessionIdStringRegistry : public IOnlineSessionIdRegistry
{
public:
	// Begin IOnlineSessionIdRegistry
	virtual inline FString ToLogString(const FOnlineSessionIdHandle& Handle) const override
	{
		FString IdValue = BasicRegistry.FindIdValue(Handle);

		if (IdValue.Len() == 0)
		{
			IdValue = FString(TEXT("[InvalidSessionID]"));
		}

		return IdValue;
	};

	virtual inline TArray<uint8> ToReplicationData(const FOnlineSessionIdHandle& Handle) const override
	{
		const FString IdValue = BasicRegistry.FindIdValue(Handle);
		const FTCHARToUTF8 IdValueUtf8(IdValue);

		TArray<uint8> ReplicationData;
		ReplicationData.SetNumUninitialized(IdValueUtf8.Length());

		FMemory::Memcpy(ReplicationData.GetData(), IdValueUtf8.Get(), IdValueUtf8.Length());

		return ReplicationData;
	}

	virtual inline FOnlineSessionIdHandle FromReplicationData(const TArray<uint8>& ReplicationData) override
	{
		const FUTF8ToTCHAR IdValueTCHAR((char*)ReplicationData.GetData(), ReplicationData.Num());
		const FString IdValue = FString(IdValueTCHAR.Length(), IdValueTCHAR.Get());

		if (!IdValue.IsEmpty())
		{
			return BasicRegistry.FindOrAddHandle(IdValue);
		}

		return FOnlineSessionIdHandle();
	}
	// End IOnlineSessionIdRegistry

	virtual ~TOnlineSessionIdStringRegistry() = default;

public:
	TOnlineBasicSessionIdRegistry<FString, OnlineServicesType> BasicRegistry;
};

class ONLINESERVICESCOMMON_API FSessionsCommon : public TOnlineComponent<ISessions>
{
public:
	using Super = ISessions;

	FSessionsCommon(FOnlineServicesCommon& InServices);

	// TOnlineComponent
	virtual void Initialize() override;
	virtual void RegisterCommands() override;

	// ISessions
	virtual TOnlineResult<FGetAllSessions> GetAllSessions(FGetAllSessions::Params&& Params) const override;
	virtual TOnlineResult<FGetSessionByName> GetSessionByName(FGetSessionByName::Params&& Params) const override;
	virtual TOnlineResult<FGetSessionById> GetSessionById(FGetSessionById::Params&& Params) const override;
	virtual TOnlineAsyncOpHandle<FCreateSession> CreateSession(FCreateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUpdateSession> UpdateSession(FUpdateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FLeaveSession> LeaveSession(FLeaveSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FFindSessions> FindSessions(FFindSessions::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FStartMatchmaking> StartMatchmaking(FStartMatchmaking::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FJoinSession> JoinSession(FJoinSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FSendSessionInvite> SendSessionInvite(FSendSessionInvite::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FRejectSessionInvite> RejectSessionInvite(FRejectSessionInvite::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FRegisterPlayers> RegisterPlayers(FRegisterPlayers::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUnregisterPlayers> UnregisterPlayers(FUnregisterPlayers::Params&& Params) override;

	virtual TOnlineEvent<void(const FSessionJoined&)> OnSessionJoined() override;
	virtual TOnlineEvent<void(const FSessionLeft&)> OnSessionLeft() override;
	virtual TOnlineEvent<void(const FSessionUpdated&)> OnSessionUpdated() override;
	virtual TOnlineEvent<void(const FSessionInviteReceived&)> OnSessionInviteReceived() override;
	virtual TOnlineEvent<void(const FUISessionJoinRequested&)> OnUISessionJoinRequested() override;

protected:
	FOnlineError CheckCreateSessionParams(const FCreateSession::Params& Params);
	FOnlineError CheckCreateSessionState(const FCreateSession::Params& Params);

	FOnlineError CheckUpdateSessionState(const FUpdateSession::Params& Params);

	FOnlineError CheckFindSessionsParams(const FFindSessions::Params& Params);
	FOnlineError CheckFindSessionsState(const FFindSessions::Params& Params);

	FOnlineError CheckJoinSessionParams(const FJoinSession::Params& Params);
	FOnlineError CheckJoinSessionState(const FJoinSession::Params& Params);

	FOnlineError CheckLeaveSessionState(const FLeaveSession::Params& Params);

protected:

	struct FSessionEvents
	{
		TOnlineEventCallable<void(const FSessionJoined&)> OnSessionJoined;
		TOnlineEventCallable<void(const FSessionLeft&)> OnSessionLeft;
		TOnlineEventCallable<void(const FSessionUpdated&)> OnSessionUpdated;
		TOnlineEventCallable<void(const FSessionInviteReceived&)> OnSessionInviteReceived;
		TOnlineEventCallable<void(const FUISessionJoinRequested&)> OnUISessionJoinRequested;
	} SessionEvents;

	TMap<FName, TSharedRef<FSession>> SessionsByName;
	TMap<FOnlineSessionIdHandle, TSharedRef<FSession>> SessionsById;

	TSharedPtr<FFindSessions::Result> CurrentSessionSearch;
	TSharedPtr<TOnlineAsyncOp<FFindSessions>> CurrentSessionSearchHandle;
};

/* UE::Online */ }

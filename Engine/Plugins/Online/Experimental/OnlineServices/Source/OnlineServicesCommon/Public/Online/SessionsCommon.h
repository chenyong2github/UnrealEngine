// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/Sessions.h"
#include "Online/OnlineComponent.h"

namespace UE::Online {

	class FOnlineServicesCommon;
	class FAccountInfo;

	struct FSessionEvents
	{
		TOnlineEventCallable<void(const FSessionJoined&)> OnSessionJoined;
		TOnlineEventCallable<void(const FSessionLeft&)> OnSessionLeft;
		TOnlineEventCallable<void(const FSessionUpdated&)> OnSessionUpdated;
		TOnlineEventCallable<void(const FInviteReceived&)> OnInviteReceived;
		TOnlineEventCallable<void(const FInviteAccepted&)> OnInviteAccepted;
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
		virtual TOnlineResult<FGetAllSessions> GetAllSessions(FGetAllSessions::Params&& Params) override;
		virtual TOnlineResult<FGetSessionByName> GetSessionByName(FGetSessionByName::Params&& Params) override;
		virtual TOnlineResult<FGetSessionById> GetSessionById(FGetSessionById::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FCreateSession> CreateSession(FCreateSession::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FUpdateSession> UpdateSession(FUpdateSession::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FLeaveSession> LeaveSession(FLeaveSession::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FFindSessions> FindSessions(FFindSessions::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FStartMatchmaking> StartMatchmaking(FStartMatchmaking::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FJoinSession> JoinSession(FJoinSession::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FSendSessionInvite> SendSessionInvite(FSendSessionInvite::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FQuerySessionInvites> QuerySessionInvites(FQuerySessionInvites::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FRejectSessionInvite> RejectSessionInvite(FRejectSessionInvite::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FRegisterPlayers> RegisterPlayers(FRegisterPlayers::Params&& Params) override;
		virtual TOnlineAsyncOpHandle<FUnregisterPlayers> UnregisterPlayers(FUnregisterPlayers::Params&& Params) override;

		virtual TOnlineEvent<void(const FSessionJoined&)> OnSessionJoined() override;
		virtual TOnlineEvent<void(const FSessionLeft&)> OnSessionLeft() override;
		virtual TOnlineEvent<void(const FSessionUpdated&)> OnSessionUpdated() override;
		virtual TOnlineEvent<void(const FInviteReceived&)> OnInviteReceived() override;
		virtual TOnlineEvent<void(const FInviteAccepted&)> OnInviteAccepted() override;

	protected:
		FSessionEvents SessionEvents;
};

/* UE::Online */ }

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsCommon.h"

#include "Online/Auth.h"
#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

	FSessionsCommon::FSessionsCommon(FOnlineServicesCommon& InServices)
		: TOnlineComponent(TEXT("Sessions"), InServices)
	{
	}

	void FSessionsCommon::Initialize()
	{
		TOnlineComponent<ISessions>::Initialize();
	}

	void FSessionsCommon::RegisterCommands()
	{
		TOnlineComponent<ISessions>::RegisterCommands();

		RegisterCommand(&FSessionsCommon::GetAllSessions);
		RegisterCommand(&FSessionsCommon::GetSessionByName);
		RegisterCommand(&FSessionsCommon::GetSessionById);
		RegisterCommand(&FSessionsCommon::CreateSession);
		RegisterCommand(&FSessionsCommon::UpdateSession);
		RegisterCommand(&FSessionsCommon::LeaveSession);
		RegisterCommand(&FSessionsCommon::FindSessions);
		RegisterCommand(&FSessionsCommon::StartMatchmaking);
		RegisterCommand(&FSessionsCommon::JoinSession);
		RegisterCommand(&FSessionsCommon::SendSessionInvite);
		RegisterCommand(&FSessionsCommon::QuerySessionInvites);
		RegisterCommand(&FSessionsCommon::RejectSessionInvite);
		RegisterCommand(&FSessionsCommon::RegisterPlayers);
		RegisterCommand(&FSessionsCommon::UnregisterPlayers);
	}

	TOnlineResult<FGetAllSessions> FSessionsCommon::GetAllSessions(FGetAllSessions::Params&& Params)
	{
		return TOnlineResult<FGetAllSessions>(Errors::NotImplemented());
	}

	TOnlineResult<FGetSessionByName> FSessionsCommon::GetSessionByName(FGetSessionByName::Params&& Params)
	{
		return TOnlineResult<FGetSessionByName>(Errors::NotImplemented());
	}

	TOnlineResult<FGetSessionById> FSessionsCommon::GetSessionById(FGetSessionById::Params&& Params)
	{
		return TOnlineResult<FGetSessionById>(Errors::NotImplemented());
	}

	TOnlineAsyncOpHandle<FCreateSession> FSessionsCommon::CreateSession(FCreateSession::Params&& Params)
	{
		TOnlineAsyncOpRef<FCreateSession> Operation = GetOp<FCreateSession>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineAsyncOpHandle<FUpdateSession> FSessionsCommon::UpdateSession(FUpdateSession::Params&& Params)
	{
		TOnlineAsyncOpRef<FUpdateSession> Operation = GetOp<FUpdateSession>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineAsyncOpHandle<FLeaveSession> FSessionsCommon::LeaveSession(FLeaveSession::Params&& Params)
	{
		TOnlineAsyncOpRef<FLeaveSession> Operation = GetOp<FLeaveSession>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineAsyncOpHandle<FFindSessions> FSessionsCommon::FindSessions(FFindSessions::Params&& Params)
	{
		TOnlineAsyncOpRef<FFindSessions> Operation = GetOp<FFindSessions>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineAsyncOpHandle<FStartMatchmaking> FSessionsCommon::StartMatchmaking(FStartMatchmaking::Params&& Params)
	{
		TOnlineAsyncOpRef<FStartMatchmaking> Operation = GetOp<FStartMatchmaking>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineAsyncOpHandle<FJoinSession> FSessionsCommon::JoinSession(FJoinSession::Params&& Params)
	{
		TOnlineAsyncOpRef<FJoinSession> Operation = GetOp<FJoinSession>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineAsyncOpHandle<FSendSessionInvite> FSessionsCommon::SendSessionInvite(FSendSessionInvite::Params&& Params)
	{
		TOnlineAsyncOpRef<FSendSessionInvite> Operation = GetOp<FSendSessionInvite>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineAsyncOpHandle<FQuerySessionInvites> FSessionsCommon::QuerySessionInvites(FQuerySessionInvites::Params&& Params)
	{
		TOnlineAsyncOpRef<FQuerySessionInvites> Operation = GetOp<FQuerySessionInvites>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineAsyncOpHandle<FRejectSessionInvite> FSessionsCommon::RejectSessionInvite(FRejectSessionInvite::Params&& Params)
	{
		TOnlineAsyncOpRef<FRejectSessionInvite> Operation = GetOp<FRejectSessionInvite>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineAsyncOpHandle<FRegisterPlayers> FSessionsCommon::RegisterPlayers(FRegisterPlayers::Params&& Params)
	{
		TOnlineAsyncOpRef<FRegisterPlayers> Operation = GetOp<FRegisterPlayers>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineAsyncOpHandle<FUnregisterPlayers> FSessionsCommon::UnregisterPlayers(FUnregisterPlayers::Params&& Params)
	{
		TOnlineAsyncOpRef<FUnregisterPlayers> Operation = GetOp<FUnregisterPlayers>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineEvent<void(const FSessionJoined&)> FSessionsCommon::OnSessionJoined()
	{
		return SessionEvents.OnSessionJoined;
	}

	TOnlineEvent<void(const FSessionLeft&)> FSessionsCommon::OnSessionLeft()
	{
		return SessionEvents.OnSessionLeft;
	}

	TOnlineEvent<void(const FSessionUpdated&)> FSessionsCommon::OnSessionUpdated()
	{
		return SessionEvents.OnSessionUpdated;
	}

	TOnlineEvent<void(const FInviteReceived&)> FSessionsCommon::OnInviteReceived()
	{
		return SessionEvents.OnInviteReceived;
	}

	TOnlineEvent<void(const FInviteAccepted&)> FSessionsCommon::OnInviteAccepted()
	{
		return SessionEvents.OnInviteAccepted;
	}

/* UE::Online */ }
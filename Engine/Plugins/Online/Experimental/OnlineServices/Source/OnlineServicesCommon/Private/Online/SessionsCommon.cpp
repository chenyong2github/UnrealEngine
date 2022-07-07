// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsCommon.h"

#include "Online/Auth.h"
#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

	/** FSessionsCommon */
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

	TOnlineResult<FGetAllSessions> FSessionsCommon::GetAllSessions(FGetAllSessions::Params&& Params) const
	{
		FGetAllSessions::Result Result;

		for (const TPair<FName, TSharedRef<FSession>>& SessionPair : SessionsByName)
		{
			Result.Sessions.Add(SessionPair.Value);
		}

		return TOnlineResult<FGetAllSessions>(MoveTemp(Result));
	}

	TOnlineResult<FGetSessionByName> FSessionsCommon::GetSessionByName(FGetSessionByName::Params&& Params) const
	{
		if (const TSharedRef<FSession>* FoundSession = SessionsByName.Find(Params.LocalName))
		{
			return TOnlineResult<FGetSessionByName>({ *FoundSession });
		}
		else
		{
			return TOnlineResult<FGetSessionByName>(Errors::NotFound());
		}
	}

	TOnlineResult<FGetSessionById> FSessionsCommon::GetSessionById(FGetSessionById::Params&& Params) const
	{
		if (const TSharedRef<FSession>* FoundSession = SessionsById.Find(Params.IdHandle))
		{
			return TOnlineResult<FGetSessionById>({ *FoundSession });
		}
		else
		{
			return TOnlineResult<FGetSessionById>(Errors::NotFound());
		}
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

	// TODO: The logic for player registration vs adding a new session member is likely to be updated
	TOnlineAsyncOpHandle<FRegisterPlayers> FSessionsCommon::RegisterPlayers(FRegisterPlayers::Params&& Params)
	{
		TOnlineAsyncOpRef<FRegisterPlayers> Op = GetOp<FRegisterPlayers>(MoveTemp(Params));

		Op->Then([this](TOnlineAsyncOp<FRegisterPlayers>& Op) mutable
		{
			const FRegisterPlayers::Params& OpParams = Op.GetParams();

			if (TSharedRef<FSession>* FoundSession = SessionsByName.Find(OpParams.SessionName))
			{
				FSessionSettings& SessionSettings = FoundSession->Get().SessionSettings;

				for (const FOnlineAccountIdHandle& User : OpParams.TargetUsers)
				{
					FRegisteredUser& RegisteredUser = SessionSettings.RegisteredUsers.FindOrAdd(User);

					// RegisterPlayers will not revoke a slot reserved for a registered user
					if (!RegisteredUser.bHasReservedSlot && OpParams.bReserveSlot)
					{
						bool bSlotReserved = false;
						if (SessionSettings.NumOpenPublicConnections > 0)
						{
							SessionSettings.NumOpenPublicConnections--;
							bSlotReserved = true;
						}
						else if (SessionSettings.NumOpenPrivateConnections > 0)
						{
							SessionSettings.NumOpenPrivateConnections--;
							bSlotReserved = true;
						}

						RegisteredUser.bHasReservedSlot = bSlotReserved;
					}

					if (const FSessionMember* SessionMember = SessionSettings.SessionMembers.Find(User))
					{
						RegisteredUser.bIsInSession = true;
					}
				}

				Op.SetResult(FRegisterPlayers::Result{});
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::RegisterPlayers] Unable to register players. Session with name [%s] not found"), *OpParams.SessionName.ToString());

				Op.SetError(Errors::NotFound());
			}
		})
		.Enqueue(GetSerialQueue());

		return Op->GetHandle();
	}

	// TODO: The logic for player registration removal vs removing a session member is likely to be updated
	TOnlineAsyncOpHandle<FUnregisterPlayers> FSessionsCommon::UnregisterPlayers(FUnregisterPlayers::Params&& Params)
	{
		TOnlineAsyncOpRef<FUnregisterPlayers> Op = GetOp<FUnregisterPlayers>(MoveTemp(Params));

		Op->Then([this](TOnlineAsyncOp<FUnregisterPlayers>& Op) mutable
		{
			const FUnregisterPlayers::Params& OpParams = Op.GetParams();

			if (TSharedRef<FSession>* FoundSession = SessionsByName.Find(OpParams.SessionName))
			{
				FSessionSettings& SessionSettings = FoundSession->Get().SessionSettings;

				for (const FOnlineAccountIdHandle& User : OpParams.TargetUsers)
				{
					if (const FRegisteredUser* RegisteredUser = SessionSettings.RegisteredUsers.Find(User))
					{
						SessionSettings.RegisteredUsers.Remove(User);

						// TODO: This logic will move to RemoveSessionMember
						bool bRemoveUserFromSession = RegisteredUser->bIsInSession && OpParams.bRemoveUnregisteredPlayers;

						if (bRemoveUserFromSession)
						{
							SessionSettings.SessionMembers.Remove(User);
						}

						if (RegisteredUser->bHasReservedSlot || bRemoveUserFromSession)
						{
							if (SessionSettings.NumOpenPublicConnections < SessionSettings.NumMaxPublicConnections)
							{
								SessionSettings.NumOpenPublicConnections++;
							}
							else if (SessionSettings.NumOpenPrivateConnections < SessionSettings.NumMaxPrivateConnections)
							{
								SessionSettings.NumOpenPrivateConnections++;
							}
						}
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::UnregisterPlayers] Unable to unregister player with id [%s]. Player not registered"), *ToLogString(User));

						Op.SetError(Errors::NotFound());
						return;
					}
				}

				Op.SetResult(FUnregisterPlayers::Result{});
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::UnregisterPlayers] Unable to unregister players. Session with name [%s] not found"), *OpParams.SessionName.ToString());

				Op.SetError(Errors::NotFound());
			}
		})
		.Enqueue(GetSerialQueue());

		return Op->GetHandle();
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

	FOnlineError FSessionsCommon::CheckCreateSessionParams(const FCreateSession::Params& Params)
	{
		if (Params.SessionSettings.NumMaxPrivateConnections == 0 && Params.SessionSettings.NumMaxPublicConnections == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionParams] Could not create session with no valid NumMaxPrivateConnections [%d] or NumMaxPublicConnections [%d]"), Params.SessionSettings.NumMaxPrivateConnections, Params.SessionSettings.NumMaxPublicConnections);

			return Errors::InvalidParams();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckCreateSessionState(const FCreateSession::Params& Params)
	{
		if (TSharedRef<FSession>* FoundSession = SessionsByName.Find(Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionState] Could not create session with name [%s]. A session with that name already exists"), *Params.SessionName.ToString());

			return Errors::InvalidState();
		}

		if (!Params.SessionSettings.IsDedicatedServerSession)
		{
			IAuthPtr Auth = Services.GetAuthInterface();

			FAuthGetAccountByAccountId::Params GetAccountByAccountIParams{ Params.LocalUserId };
			TOnlineResult<FAuthGetAccountByAccountId> Result = Auth->GetAccountByAccountId(MoveTemp(GetAccountByAccountIParams));

			if (Result.IsOk())
			{
				if (Result.GetOkValue().AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionState] Could not create session with user [%s] not logged in"), *ToLogString(Params.LocalUserId));

					return Errors::InvalidUser();
				}
			}
		}

		// User login check for all local users
		IAuthPtr Auth = Services.GetAuthInterface();

		TArray<FOnlineAccountIdHandle> LocalUserIds;
		LocalUserIds.Reserve(Params.LocalUsers.Num());
		Params.LocalUsers.GenerateKeyArray(LocalUserIds);

		for (const FOnlineAccountIdHandle& LocalUserId : LocalUserIds)
		{
			FAuthGetAccountByAccountId::Params GetAccountByAccountIParams{ LocalUserId };
			TOnlineResult<FAuthGetAccountByAccountId> Result = Auth->GetAccountByAccountId(MoveTemp(GetAccountByAccountIParams));

			if (Result.IsOk())
			{
				if (Result.GetOkValue().AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionState] Could not create session with user [%s] not logged in"), *ToLogString(LocalUserId));

					return Errors::InvalidUser();
				}
			}
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckUpdateSessionState(const FUpdateSession::Params& Params)
	{
		if (TSharedRef<FSession>* FoundSession = SessionsByName.Find(Params.SessionName))
		{
			if (!FoundSession->Get().SessionSettings.IsDedicatedServerSession)
			{
				IAuthPtr Auth = Services.GetAuthInterface();

				FAuthGetAccountByAccountId::Params GetAccountByAccountIParams{ Params.LocalUserId };
				TOnlineResult<FAuthGetAccountByAccountId> Result = Auth->GetAccountByAccountId(MoveTemp(GetAccountByAccountIParams));

				if (Result.IsOk())
				{
					if (Result.GetOkValue().AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
					{
						UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::CheckUpdateSessionState] Could not update session with user [%s] not logged in"), *ToLogString(Params.LocalUserId));

						return Errors::InvalidUser();
					}
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckUpdateSessionState] Could not update session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Errors::NotFound();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckFindSessionsParams(const FFindSessions::Params& Params)
	{
		if (Params.MaxResults <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckFindSessionsParams] Could not find sessions with no valid MaxResults [%d]"), Params.MaxResults);

			return Errors::InvalidParams();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckFindSessionsState(const FFindSessions::Params& Params)
	{
		// User login check
		IAuthPtr Auth = Services.GetAuthInterface();

		FAuthGetAccountByAccountId::Params GetAccountByAccountIParams{ Params.LocalUserId };
		TOnlineResult<FAuthGetAccountByAccountId> Result = Auth->GetAccountByAccountId(MoveTemp(GetAccountByAccountIParams));

		if (Result.IsOk())
		{
			if (Result.GetOkValue().AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckFindSessionsState] Could not find sessions with user [%s] not logged in"), *ToLogString(Params.LocalUserId));

				return Errors::InvalidUser();
			}
		}

		// Ongoing search check
		if (CurrentSessionSearch.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckFindSessionsState] Could not find sessions, search already in progress"));

			return Errors::AlreadyPending();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckJoinSessionParams(const FJoinSession::Params& Params)
	{
		const FSessionSettings& SessionSettings = Params.Session->SessionSettings;

		TArray<FOnlineAccountIdHandle> LocalUserIds;
		LocalUserIds.Reserve(Params.LocalUsers.Num());
		Params.LocalUsers.GenerateKeyArray(LocalUserIds);

		for (const FOnlineAccountIdHandle& LocalUserId : LocalUserIds)
		{
			if (SessionSettings.SessionMembers.Contains(LocalUserId))
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session. User [%s] already in session"), *ToLogString(LocalUserId));

				return Errors::AccessDenied();
			}
		}

		if (!SessionSettings.bAllowNewMembers)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session. bAllowNewMembers is set to false"));

			return Errors::AccessDenied();
		}

		if (!SessionSettings.bAllowUnregisteredPlayers)
		{
			for (const FOnlineAccountIdHandle& LocalUserId : LocalUserIds)
			{
				if (!SessionSettings.RegisteredUsers.Contains(LocalUserId))
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session. bAllowUnregisteredPlayers is set to false and user [%s] is not registered"), *ToLogString(LocalUserId));

					return Errors::AccessDenied();
				}
			}
		}

		if (SessionSettings.NumMaxPublicConnections > 0 && SessionSettings.NumOpenPublicConnections < (uint32)LocalUserIds.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session. Not enough NumOpenPublicConnections [%s] for the amount of joining players [%d]"), SessionSettings.NumOpenPublicConnections, LocalUserIds.Num());

			return Errors::AccessDenied();
		}

		if (SessionSettings.NumMaxPrivateConnections > 0 && SessionSettings.NumOpenPrivateConnections < (uint32)LocalUserIds.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session. Not enough NumOpenPrivateConnections [%s] for the amount of joining players [%d]"), SessionSettings.NumOpenPrivateConnections, LocalUserIds.Num());

			return Errors::AccessDenied();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckJoinSessionState(const FJoinSession::Params& Params)
	{
		// Check if a session with that name already exists
		if (TSharedRef<FSession>* FoundSession = SessionsByName.Find(Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session with name [%s]. A session with that name already exists"), *Params.SessionName.ToString());

			return Errors::InvalidState(); // TODO: New error: Session with name %s already exists
		}

		// User login check for all local users
		IAuthPtr Auth = Services.GetAuthInterface();

		TArray<FOnlineAccountIdHandle> LocalUserIds;
		LocalUserIds.Reserve(Params.LocalUsers.Num());
		Params.LocalUsers.GenerateKeyArray(LocalUserIds);

		for (const FOnlineAccountIdHandle& LocalUserId : LocalUserIds)
		{
			FAuthGetAccountByAccountId::Params GetAccountByAccountIParams{ LocalUserId };
			TOnlineResult<FAuthGetAccountByAccountId> Result = Auth->GetAccountByAccountId(MoveTemp(GetAccountByAccountIParams));

			if (Result.IsOk())
			{
				if (Result.GetOkValue().AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session with user [%s] not logged in"), *ToLogString(LocalUserId));

					return Errors::InvalidUser();
				}
			}
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckLeaveSessionState(const FLeaveSession::Params& Params)
	{
		// User login check for main caller, session check
		if (TSharedRef<FSession>* FoundSession = SessionsByName.Find(Params.SessionName))
		{
			if (!FoundSession->Get().SessionSettings.IsDedicatedServerSession)
			{
				IAuthPtr Auth = Services.GetAuthInterface();

				FAuthGetAccountByAccountId::Params GetAccountByAccountIParams{ Params.LocalUserId };
				TOnlineResult<FAuthGetAccountByAccountId> Result = Auth->GetAccountByAccountId(MoveTemp(GetAccountByAccountIParams));

				if (Result.IsOk())
				{
					if (Result.GetOkValue().AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
					{
						UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::CheckLeaveSessionState] Could not leave session with user [%s] not logged in"), *ToLogString(Params.LocalUserId));

						return Errors::InvalidUser();
					}
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckLeaveSessionState] Could not leave session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Errors::NotFound();
		}

		// User login check for all local users
		IAuthPtr Auth = Services.GetAuthInterface();

		for (const FOnlineAccountIdHandle& LocalUserId : Params.LocalUsers)
		{
			FAuthGetAccountByAccountId::Params GetAccountByAccountIParams{ LocalUserId };
			TOnlineResult<FAuthGetAccountByAccountId> Result = Auth->GetAccountByAccountId(MoveTemp(GetAccountByAccountIParams));

			if (Result.IsOk())
			{
				if (Result.GetOkValue().AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckLeaveSessionState] Could not leave session with user [%s] not logged in"), *ToLogString(LocalUserId));

					return Errors::InvalidUser();
				}
			}
		}

		return Errors::Success();
	}

/* UE::Online */ }
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
		//RegisterCommand(&FSessionsCommon::JoinSession);
		RegisterCommand(&FSessionsCommon::SendSessionInvite);
		//RegisterCommand(&FSessionsCommon::RejectSessionInvite);
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

	TOnlineAsyncOpHandle<FAddSessionMembers> FSessionsCommon::AddSessionMembers(FAddSessionMembers::Params&& Params)
	{
		TOnlineAsyncOpRef<FAddSessionMembers> Op = GetOp<FAddSessionMembers>(MoveTemp(Params));

		Op->Then([this](TOnlineAsyncOp<FAddSessionMembers>& Op) mutable
		{
			const FAddSessionMembers::Params& OpParams = Op.GetParams();

			FOnlineError StateCheck = CheckAddSessionMembersState(OpParams);
			if (StateCheck != Errors::Success())
			{
				Op.SetError(MoveTemp(StateCheck));
				return;
			}

			TOnlineResult<FAddSessionMembers> Result = AddSessionMembersImpl(OpParams);
			if (Result.IsOk())
			{
				Op.SetResult(MoveTemp(Result.GetOkValue()));
			}
			else
			{
				Op.SetError(MoveTemp(Result.GetErrorValue()));
			}
		})
		.Enqueue(GetSerialQueue());

		return Op->GetHandle();
	}

	TOnlineAsyncOpHandle<FRemoveSessionMembers> FSessionsCommon::RemoveSessionMembers(FRemoveSessionMembers::Params&& Params)
	{
		TOnlineAsyncOpRef<FRemoveSessionMembers> Op = GetOp<FRemoveSessionMembers>(MoveTemp(Params));

		Op->Then([this](TOnlineAsyncOp<FRemoveSessionMembers>& Op) mutable
		{
			const FRemoveSessionMembers::Params& OpParams = Op.GetParams();

			FOnlineError StateCheck = CheckRemoveSessionMembersState(OpParams);
			if (StateCheck != Errors::Success())
			{
				Op.SetError(MoveTemp(StateCheck));
				return;
			}

			TOnlineResult<FRemoveSessionMembers> Result = RemoveSessionMembersImpl(OpParams);
			if (Result.IsOk())
			{
				Op.SetResult(MoveTemp(Result.GetOkValue()));
			}
			else
			{
				Op.SetError(MoveTemp(Result.GetErrorValue()));
			}
		})
		.Enqueue(GetSerialQueue());

		return Op->GetHandle();
	}

	TOnlineAsyncOpHandle<FSendSessionInvite> FSessionsCommon::SendSessionInvite(FSendSessionInvite::Params&& Params)
	{
		TOnlineAsyncOpRef<FSendSessionInvite> Operation = GetOp<FSendSessionInvite>(MoveTemp(Params));
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

			FOnlineError StateCheck = CheckRegisterPlayersState(OpParams);
			if (StateCheck != Errors::Success())
			{
				Op.SetError(MoveTemp(StateCheck));
				return;
			}

			TOnlineResult<FRegisterPlayers> Result = RegisterPlayersImpl(OpParams);
			if (Result.IsOk())
			{
				Op.SetResult(MoveTemp(Result.GetOkValue()));
			}
			else
			{
				Op.SetError(MoveTemp(Result.GetErrorValue()));
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

			FOnlineError StateCheck = CheckUnregisterPlayersState(OpParams);
			if (StateCheck != Errors::Success())
			{
				Op.SetError(MoveTemp(StateCheck));
				return;
			}

			TOnlineResult<FUnregisterPlayers> Result = UnregisterPlayersImpl(OpParams);
			if (Result.IsOk())
			{
				Op.SetResult(MoveTemp(Result.GetOkValue()));
			}
			else
			{
				Op.SetError(MoveTemp(Result.GetErrorValue()));
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

	TOnlineEvent<void(const FSessionInviteReceived&)> FSessionsCommon::OnSessionInviteReceived()
	{
		return SessionEvents.OnSessionInviteReceived;
	}

	TOnlineEvent<void(const FUISessionJoinRequested&)> FSessionsCommon::OnUISessionJoinRequested()
	{
		return SessionEvents.OnUISessionJoinRequested;
	}

	TOnlineResult<FAddSessionMembers> FSessionsCommon::AddSessionMembersImpl(const FAddSessionMembers::Params& Params)
	{
		TSharedRef<FSession> FoundSession = SessionsByName.FindChecked(Params.SessionName);

		FSessionSettings& SessionSettings = FoundSession->SessionSettings;

		for (const TPair<FOnlineAccountIdHandle, FSessionMember>& Entry : Params.NewSessionMembers)
		{
			const FOnlineAccountIdHandle& SessionMemberId = Entry.Key;
			const FSessionMember& SessionMember = Entry.Value;

			// If the user was already registered in the session, we'll update it and reserve a slot for it
			if (FRegisteredPlayer* RegisteredPlayer = SessionSettings.RegisteredPlayers.Find(SessionMemberId))
			{
				RegisteredPlayer->bIsInSession = true;

				if (!RegisteredPlayer->bHasReservedSlot)
				{
					bool bSlotReserved = false;

					if (SessionSettings.NumOpenPublicConnections > 0)
					{
						SessionSettings.NumOpenPublicConnections--;
						bSlotReserved = true;
					}
					else
					{
						return TOnlineResult<FAddSessionMembers>(Errors::InvalidState());
					}

					// TODO: Discuss usage for Private Connections

					RegisteredPlayer->bHasReservedSlot = bSlotReserved;
				}
			}
			else if (Params.bRegisterPlayers) // If it wasn't and the parameters dictate it, we'll register them with a reserved slot
			{
				FRegisterPlayers::Params RegisterPlayerParams = { Params.SessionName, { SessionMemberId }, true };

				TOnlineResult<FRegisterPlayers> Result = RegisterPlayersImpl(RegisterPlayerParams);
				if (Result.IsError())
				{
					return TOnlineResult<FAddSessionMembers>(MoveTemp(Result.GetErrorValue()));
				}
			}
			else // Even if the new session member is not a registered player, we'll still need to decrease the number of open slots
			{
				if (SessionSettings.NumOpenPublicConnections > 0)
				{
					SessionSettings.NumOpenPublicConnections--;
				}
				else
				{
					return TOnlineResult<FAddSessionMembers>(Errors::InvalidState());
				}

				// TODO: Discuss usage for Private Connections
			}

			SessionSettings.SessionMembers.Emplace(SessionMemberId, SessionMember);
		}

		return TOnlineResult<FAddSessionMembers>();
	}

	TOnlineResult<FRemoveSessionMembers> FSessionsCommon::RemoveSessionMembersImpl(const FRemoveSessionMembers::Params& Params)
	{
		TSharedRef<FSession> FoundSession = SessionsByName.FindChecked(Params.SessionName);

		FSessionSettings& SessionSettings = FoundSession->SessionSettings;

		for (const FOnlineAccountIdHandle& SessionMemberId : Params.SessionMemberIds)
		{
			if (Params.bUnregisterPlayers) // If the parameters dictate it, well unregister the player as well
			{
				FUnregisterPlayers::Params UnregisterPlayersParams = { Params.SessionName, { SessionMemberId }, false };

				TOnlineResult<FUnregisterPlayers> Result = UnregisterPlayersImpl(UnregisterPlayersParams);
				if (Result.IsError())
				{
					return TOnlineResult<FRemoveSessionMembers>(MoveTemp(Result.GetErrorValue()));
				}
			}
			else if (FRegisteredPlayer* RegisteredPlayer = SessionSettings.RegisteredPlayers.Find(SessionMemberId)) // If the removed session member is still registered, we'll update it and keep their reserved slot
			{
				RegisteredPlayer->bIsInSession = false;
			}
			else  // If the removed session member is not a registered player with a reserved slot, we'll increase the number of open slots
			{
				if (SessionSettings.NumOpenPublicConnections < SessionSettings.NumMaxPublicConnections)
				{
					SessionSettings.NumOpenPublicConnections++;
				}
				else
				{
					return TOnlineResult<FRemoveSessionMembers>(Errors::InvalidState());
				}

				// TODO: Discuss usage for Private Connections
			}

			SessionSettings.SessionMembers.Remove(SessionMemberId);
		}

		return TOnlineResult<FRemoveSessionMembers>();
	}

	TOnlineResult<FRegisterPlayers> FSessionsCommon::RegisterPlayersImpl(const FRegisterPlayers::Params& Params)
	{
		TSharedRef<FSession> FoundSession = SessionsByName.FindChecked(Params.SessionName);

		FSessionSettings& SessionSettings = FoundSession->SessionSettings;

		for (const FOnlineAccountIdHandle& User : Params.TargetUsers)
		{
			FRegisteredPlayer& RegisteredPlayer = SessionSettings.RegisteredPlayers.FindOrAdd(User);

			if (const FSessionMember* SessionMember = SessionSettings.SessionMembers.Find(User))
			{
				RegisteredPlayer.bIsInSession = true;
			}

			// RegisterPlayers will not revoke a slot reserved for a registered player
			if (!RegisteredPlayer.bIsInSession && !RegisteredPlayer.bHasReservedSlot && Params.bReserveSlot)
			{
				bool bSlotReserved = false;
				
				if (SessionSettings.NumOpenPublicConnections > 0)
				{
					SessionSettings.NumOpenPublicConnections--;
					bSlotReserved = true;
				}
				else
				{
					return TOnlineResult<FRegisterPlayers>(Errors::InvalidState());
				}

				// TODO: Discuss usage for Private Connections

				RegisteredPlayer.bHasReservedSlot = bSlotReserved;
			}			
		}

		return TOnlineResult<FRegisterPlayers>();
	}

	TOnlineResult<FUnregisterPlayers> FSessionsCommon::UnregisterPlayersImpl(const FUnregisterPlayers::Params& Params)
	{
		TSharedRef<FSession> FoundSession = SessionsByName.FindChecked(Params.SessionName);

		FSessionSettings& SessionSettings = FoundSession->SessionSettings;

		for (const FOnlineAccountIdHandle& User : Params.TargetUsers)
		{
			if (const FRegisteredPlayer* RegisteredPlayer = SessionSettings.RegisteredPlayers.Find(User))
			{
				// If the registered player had a reserved slot, we'll remove it
				if (RegisteredPlayer->bHasReservedSlot)
				{
					if (SessionSettings.NumOpenPublicConnections < SessionSettings.NumMaxPublicConnections)
					{
						SessionSettings.NumOpenPublicConnections++;
					}
					else
					{
						return TOnlineResult<FUnregisterPlayers>(Errors::InvalidState());
					}

					// TODO: Discuss usage for Private Connections
				}

				if (RegisteredPlayer->bIsInSession && Params.bRemoveUnregisteredPlayers)
				{
					FRemoveSessionMembers::Params RemoveSessionMembersParams = { User, Params.SessionName, Params.TargetUsers, false };

					TOnlineResult<FRemoveSessionMembers> Result = RemoveSessionMembersImpl(RemoveSessionMembersParams);
					if (Result.IsError())
					{
						return TOnlineResult<FUnregisterPlayers>(MoveTemp(Result.GetErrorValue()));
					}
				}

				SessionSettings.RegisteredPlayers.Remove(User);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::UnregisterPlayers] Unable to unregister player with id [%s]. Player not registered"), *ToLogString(User));

				return TOnlineResult<FUnregisterPlayers>(Errors::NotFound());
			}
		}

		return TOnlineResult<FUnregisterPlayers>();
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

		if (Params.SessionSettings.bPresenceEnabled)
		{
			for (const TPair<FName, TSharedRef<FSession>>& Entry : SessionsByName)
			{
				if (Entry.Value->SessionSettings.bPresenceEnabled)
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionState] Could not create session with bPresenceEnabled set to true when another already exists [%s]."), *Entry.Key.ToString());

					return Errors::InvalidState();
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
			if (!Auth->IsLoggedIn(LocalUserId))
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionState] Could not create session with user [%s] not logged in"), *ToLogString(LocalUserId));

				return Errors::InvalidUser();
			}
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckUpdateSessionState(const FUpdateSession::Params& Params)
	{
		if (TSharedRef<FSession>* FoundSession = SessionsByName.Find(Params.SessionName))
		{
			if (!FoundSession->Get().SessionSettings.bIsDedicatedServerSession)
			{
				if (!Services.GetAuthInterface()->IsLoggedIn(Params.LocalUserId))
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::CheckUpdateSessionState] Could not update session with user [%s] not logged in"), *ToLogString(Params.LocalUserId));

					return Errors::InvalidUser();
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
		if (!Services.GetAuthInterface()->IsLoggedIn(Params.LocalUserId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckFindSessionsState] Could not find sessions with user [%s] not logged in"), *ToLogString(Params.LocalUserId));

			return Errors::InvalidUser();
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

		if (Params.Session->CurrentState != ESessionState::Valid)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::FJoinSession] Could not join session with invalid session state [%s]"), LexToString(Params.Session->CurrentState));

			return Errors::InvalidParams();
		}

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
				if (!SessionSettings.RegisteredPlayers.Contains(LocalUserId))
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session. bAllowUnregisteredPlayers is set to false and user [%s] is not registered"), *ToLogString(LocalUserId));

					return Errors::AccessDenied();
				}
			}
		}

		if (SessionSettings.NumMaxPublicConnections > 0 && SessionSettings.NumOpenPublicConnections < (uint32)LocalUserIds.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session. Not enough NumOpenPublicConnections [%u] for the amount of joining players [%d]"), SessionSettings.NumOpenPublicConnections, LocalUserIds.Num());

			return Errors::AccessDenied();
		}

		if (SessionSettings.NumMaxPrivateConnections > 0 && SessionSettings.NumOpenPrivateConnections < (uint32)LocalUserIds.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session. Not enough NumOpenPrivateConnections [%u] for the amount of joining players [%d]"), SessionSettings.NumOpenPrivateConnections, LocalUserIds.Num());

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
			if(!Auth->IsLoggedIn(LocalUserId))
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session with user [%s] not logged in"), *ToLogString(LocalUserId));

				return Errors::InvalidUser();
			}
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckAddSessionMembersState(const FAddSessionMembers::Params& Params)
	{
		if (TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckAddSessionMembersState] Could not add session member to session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Result.GetValue();
		}

		// TODO: Check if there are enough slots available if Params::bReserveSlot is true

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckRemoveSessionMembersState(const FRemoveSessionMembers::Params& Params)
	{
		if (TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckRemoveSessionMembersState] Could not remove session member from session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Result.GetValue();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckLeaveSessionState(const FLeaveSession::Params& Params)
	{
		// User login check for main caller, session check
		if (!SessionsByName.Contains(Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckLeaveSessionState] Could not leave session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Errors::NotFound();
		}

		IAuthPtr Auth = Services.GetAuthInterface();

		// User login check for all local users
		for (const FOnlineAccountIdHandle& LocalUserId : Params.LocalUsers)
		{
			if (!Auth->IsLoggedIn(LocalUserId))
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckLeaveSessionState] Could not leave session with user [%s] not logged in"), *ToLogString(LocalUserId));

				return Errors::InvalidUser();
			}
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckSendSessionInviteState(const FSendSessionInvite::Params& Params)
	{
		// User login check for main caller, session check
		if (TSharedRef<FSession>* FoundSession = SessionsByName.Find(Params.SessionName))
		{
			if (!FoundSession->Get().SessionSettings.bIsDedicatedServerSession)
			{
				if (!Services.GetAuthInterface()->IsLoggedIn(Params.LocalUserId))
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::CheckSendSessionInviteState] Could not send session invite with user [%s] not logged in"), *ToLogString(Params.LocalUserId));

					return Errors::InvalidUser();
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckSendSessionInviteState] Could not send session invite for session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Errors::NotFound();
		}

		// TODO: check if Session Invite Id is valid

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckRejectSessionInviteState(const FRejectSessionInvite::Params& Params)
	{
		if (!Services.GetAuthInterface()->IsLoggedIn(Params.LocalUserId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::CheckRejectSessionInviteState] Could not send session invite with user [%s] not logged in"), *ToLogString(Params.LocalUserId));

			return Errors::InvalidUser();
		}

		// TODO: check if Session Invite Id is valid

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckRegisterPlayersState(const FRegisterPlayers::Params& Params)
	{
		if (TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckRegisterPlayersState] Could not register players in session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Result.GetValue();
		}

		// TODO: Check if there are enough slots available if Params::bReserveSlot is true

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckUnregisterPlayersState(const FUnregisterPlayers::Params& Params)
	{
		if(TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckUnregisterPlayersState] Could not unregister players from session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Result.GetValue();
		}

		return Errors::Success();
	}

	// TODO: Have all Check methods return TOptional too, change call sites, and write Macro for repeating code structure
	TOptional<FOnlineError> FSessionsCommon::CheckSessionExistsByName(const FName& SessionName)
	{
		TOptional<FOnlineError> Result;

		if (!SessionsByName.Contains(SessionName))
		{
			Result.Emplace(Errors::NotFound());
		}

		return Result;
	}

/* UE::Online */ }
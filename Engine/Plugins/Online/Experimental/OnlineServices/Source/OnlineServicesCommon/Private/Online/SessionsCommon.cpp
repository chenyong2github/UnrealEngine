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
		RegisterCommand(&FSessionsCommon::RejectSessionInvite);
		RegisterCommand(&FSessionsCommon::RegisterPlayers);
		RegisterCommand(&FSessionsCommon::UnregisterPlayers);
	}

	TOnlineResult<FGetAllSessions> FSessionsCommon::GetAllSessions(FGetAllSessions::Params&& Params) const
	{
		FGetAllSessions::Result Result;

		for (const TPair<FOnlineSessionIdHandle, TSharedRef<FSessionCommon>>& SessionPair : AllSessionsById)
		{
			Result.Sessions.Add(SessionPair.Value);
		}

		return TOnlineResult<FGetAllSessions>(MoveTemp(Result));
	}

	TOnlineResult<FGetSessionByName> FSessionsCommon::GetSessionByName(FGetSessionByName::Params&& Params) const
	{
		TOnlineResult<FGetMutableSessionByName> GetMutableSessionByNameResult = GetMutableSessionByName({ Params.LocalName });
		if (GetMutableSessionByNameResult.IsOk())
		{
			return TOnlineResult<FGetSessionByName>({ GetMutableSessionByNameResult.GetOkValue().Session });
		}
		else
		{
			return TOnlineResult<FGetSessionByName>(GetMutableSessionByNameResult.GetErrorValue());
		}	
	}

	TOnlineResult<FGetSessionById> FSessionsCommon::GetSessionById(FGetSessionById::Params&& Params) const
	{
		TOnlineResult<FGetMutableSessionById> GetMutableSessionByIdResult = GetMutableSessionById({ Params.IdHandle });
		if (GetMutableSessionByIdResult.IsOk())
		{
			return TOnlineResult<FGetSessionById>({ GetMutableSessionByIdResult.GetOkValue().Session });
			}
		else
		{
			return TOnlineResult<FGetSessionById>(GetMutableSessionByIdResult.GetErrorValue());
			}
		}

	TOnlineResult<FGetPresenceSession> FSessionsCommon::GetPresenceSession(FGetPresenceSession::Params&& Params) const
	{
		if (const FOnlineSessionIdHandle* PresenceSessionId = PresenceSessionsUserMap.Find(Params.LocalUserId))
		{
			return TOnlineResult<FGetPresenceSession>({ AllSessionsById.FindChecked(*PresenceSessionId) });
		}
		else
		{
			return  TOnlineResult<FGetPresenceSession>({ Errors::InvalidState() });
		}
	}

	TOnlineResult<FSetPresenceSession> FSessionsCommon::SetPresenceSession(FSetPresenceSession::Params&& Params)
	{
		FOnlineSessionIdHandle& PresenceSessionId = PresenceSessionsUserMap.FindOrAdd(Params.LocalUserId);
		PresenceSessionId = Params.SessionId;

		return TOnlineResult<FSetPresenceSession>(FSetPresenceSession::Result{ });
	}

	TOnlineResult<FClearPresenceSession> FSessionsCommon::ClearPresenceSession(FClearPresenceSession::Params&& Params)
	{
		PresenceSessionsUserMap.Remove(Params.LocalUserId);

		return TOnlineResult<FClearPresenceSession>(FClearPresenceSession::Result{ });
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

	TOnlineResult<FGetSessionInvites> FSessionsCommon::GetSessionInvites(FGetSessionInvites::Params&& Params)
	{
		TArray<TSharedRef<const FSessionInvite>> SessionInvites;

		if (const TMap<FOnlineSessionInviteIdHandle, TSharedRef<FSessionInvite>>* UserMap = SessionInvitesUserMap.Find(Params.LocalAccountId))
		{
			for (const TPair<FOnlineSessionInviteIdHandle, TSharedRef<FSessionInvite>>& Entry : *UserMap)
			{
				SessionInvites.Add(Entry.Value);
			}
		}
		else
		{
			return TOnlineResult<FGetSessionInvites>(Errors::NotFound());
		}

		return TOnlineResult<FGetSessionInvites>({ SessionInvites });
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

	TOnlineResult<FGetMutableSessionByName> FSessionsCommon::GetMutableSessionByName(FGetMutableSessionByName::Params&& Params) const
	{
		if (const FOnlineSessionIdHandle* SessionIdHandle = LocalSessionsByName.Find(Params.LocalName))
		{
			check(AllSessionsById.Contains(*SessionIdHandle));

			return TOnlineResult<FGetMutableSessionByName>({ AllSessionsById.FindChecked(*SessionIdHandle) });
		}
		else
		{
			return TOnlineResult<FGetMutableSessionByName>(Errors::NotFound());
		}
	}

	TOnlineResult<FGetMutableSessionById> FSessionsCommon::GetMutableSessionById(FGetMutableSessionById::Params&& Params) const
	{
		if (const TSharedRef<FSessionCommon>* FoundSession = AllSessionsById.Find(Params.IdHandle))
		{
			return TOnlineResult<FGetMutableSessionById>({ *FoundSession });
		}
		else
		{
			return TOnlineResult<FGetMutableSessionById>(Errors::NotFound());
		}
	}

	void FSessionsCommon::ClearSessionByName(const FName& SessionName)
	{
		for (const TPair<FAccountId, TArray<FName>>& Entry : NamedSessionUserMap)
		{
			if (Entry.Value.Contains(SessionName))
			{
				return;
			}
		}

		// If no references were found, we'll remove the named session entry
		LocalSessionsByName.Remove(SessionName);
	}

	void FSessionsCommon::ClearSessionById(const FOnlineSessionIdHandle& SessionId)
	{
		// PresenceSessionsUserMap is not evaluated, since any session there would also be in LocalSessionsByName
		for (const TPair<FName, FOnlineSessionIdHandle>& Entry : LocalSessionsByName)
		{
			if (Entry.Value == SessionId)
			{
				return;
			}
		}

		for (const TPair<FAccountId, TMap<FOnlineSessionInviteIdHandle, TSharedRef<FSessionInvite>>>& Entry : SessionInvitesUserMap)
		{
			for (const TPair<FOnlineSessionInviteIdHandle, TSharedRef<FSessionInvite>>& InviteMap : Entry.Value)
			{
				if (InviteMap.Value->SessionId == SessionId)
				{
					return;
				}
			}			
		}

		for (const TPair<FAccountId, TArray<FOnlineSessionIdHandle>>& Entry : SearchResultsUserMap)
		{
			if (Entry.Value.Contains(SessionId))
			{
				return;
			}
		}

		// If no references were found, we'll remove the session entry
		AllSessionsById.Remove(SessionId);
	}

	TOnlineResult<FAddSessionMembers> FSessionsCommon::AddSessionMembersImpl(const FAddSessionMembers::Params& Params)
	{
		TOnlineResult<FGetMutableSessionByName> GetMutableSessionByNameResult = GetMutableSessionByName({ Params.SessionName });
		if (GetMutableSessionByNameResult.IsError())
		{
			return TOnlineResult<FAddSessionMembers>(GetMutableSessionByNameResult.GetErrorValue());
		}
		TSharedRef<FSessionCommon> FoundSession = GetMutableSessionByNameResult.GetOkValue().Session;

		FSessionSettings& SessionSettings = FoundSession->SessionSettings;

		for (const TPair<FAccountId, FSessionMember>& Entry : Params.NewSessionMembers)
		{
			const FAccountId& SessionMemberId = Entry.Key;
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
				FRegisterPlayers::Params RegisterPlayerParams = { Params.LocalAccountId, Params.SessionName, { SessionMemberId }, true };

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
		TOnlineResult<FGetMutableSessionByName> GetMutableSessionByNameResult = GetMutableSessionByName({ Params.SessionName });
		if (GetMutableSessionByNameResult.IsError())
		{
			return TOnlineResult<FRemoveSessionMembers>(GetMutableSessionByNameResult.GetErrorValue());
		}
		TSharedRef<FSessionCommon> FoundSession = GetMutableSessionByNameResult.GetOkValue().Session;

		FSessionSettings& SessionSettings = FoundSession->SessionSettings;

		for (const FAccountId& SessionMemberId : Params.SessionMemberIds)
		{
			if (Params.bUnregisterPlayers) // If the parameters dictate it, well unregister the player as well
			{
				FUnregisterPlayers::Params UnregisterPlayersParams = { Params.LocalAccountId, Params.SessionName, { SessionMemberId }, false };

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
		TOnlineResult<FGetMutableSessionByName> GetMutableSessionByNameResult = GetMutableSessionByName({ Params.SessionName });
		if (GetMutableSessionByNameResult.IsError())
		{
			return TOnlineResult<FRegisterPlayers>(GetMutableSessionByNameResult.GetErrorValue());
		}
		TSharedRef<FSessionCommon> FoundSession = GetMutableSessionByNameResult.GetOkValue().Session;

		FSessionSettings& SessionSettings = FoundSession->SessionSettings;

		for (const FAccountId& User : Params.TargetUsers)
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
		TOnlineResult<FGetMutableSessionByName> GetMutableSessionByNameResult = GetMutableSessionByName({ Params.SessionName });
		if (GetMutableSessionByNameResult.IsError())
		{
			return TOnlineResult<FUnregisterPlayers>(GetMutableSessionByNameResult.GetErrorValue());
		}
		TSharedRef<FSessionCommon> FoundSession = GetMutableSessionByNameResult.GetOkValue().Session;

		FSessionSettings& SessionSettings = FoundSession->SessionSettings;

		for (const FAccountId& User : Params.TargetUsers)
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
		if (Params.SessionName.IsNone())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionParams] Could not create session with no valid SessionName set"));

			return Errors::InvalidParams();
		}

		if (!Params.LocalAccountId.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionParams] Could not create session with name [%s]. LocalAccountId [%s] not valid"), *Params.SessionName.ToString(), *ToLogString(Params.LocalAccountId));

			return Errors::InvalidParams();
		}

		if (!Params.LocalAccounts.Contains(Params.LocalAccountId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionParams] Could not create session with name [%s]. Missing Session Member info for user [%s]"), *Params.SessionName.ToString(), *ToLogString(Params.LocalAccountId));

			return Errors::InvalidParams();
		}

		if (Params.SessionSettings.NumMaxPrivateConnections == 0 && Params.SessionSettings.NumMaxPublicConnections == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionParams] Could not create session with name [%s] with no valid NumMaxPrivateConnections [%d] or NumMaxPublicConnections [%d]"), *Params.SessionName.ToString(), Params.SessionSettings.NumMaxPrivateConnections, Params.SessionSettings.NumMaxPublicConnections);

			return Errors::InvalidParams();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckCreateSessionState(const FCreateSession::Params& Params)
	{
		TOnlineResult<FGetSessionByName> GetSessionByNameResult = GetSessionByName({ Params.SessionName });
		if (GetSessionByNameResult.IsError())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionState] Could not create session with name [%s]. A session with that name already exists"), *Params.SessionName.ToString());

			return Errors::InvalidState();
		}

		if (Params.SessionSettings.bPresenceEnabled)
		{
			for (const TPair<FName, FOnlineSessionIdHandle>& Entry : LocalSessionsByName)
			{
				TOnlineResult<FGetSessionById> GetSessionByIdResult = GetSessionById({ Params.LocalAccountId, Entry.Value });
				if (GetSessionByIdResult.IsOk())
				{
					if (GetSessionByIdResult.GetOkValue().Session->GetSessionSettings().bPresenceEnabled)
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionState] Could not create session with bPresenceEnabled set to true when another already exists [%s]."), *Entry.Key.ToString());

					return Errors::InvalidState();
				}
			}
		}
		}

		// User login check for all local users
		IAuthPtr Auth = Services.GetAuthInterface();

		TArray<FAccountId> LocalAccountIds;
		LocalAccountIds.Reserve(Params.LocalAccounts.Num());
		Params.LocalAccounts.GenerateKeyArray(LocalAccountIds);

		for (const FAccountId& LocalAccountId : LocalAccountIds)
		{
			if (!Auth->IsLoggedIn(LocalAccountId))
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionState] Could not create session with user [%s] not logged in"), *ToLogString(LocalAccountId));

				return Errors::InvalidUser();
			}
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckUpdateSessionState(const FUpdateSession::Params& Params)
	{
		TOnlineResult<FGetSessionByName> GetSessionByNameResult = GetSessionByName({ Params.SessionName });
		if (GetSessionByNameResult.IsOk())
		{
			TSharedRef<const ISession> FoundSession = GetSessionByNameResult.GetOkValue().Session;

			if (!FoundSession->GetSessionSettings().bIsDedicatedServerSession)
			{
				if (!Services.GetAuthInterface()->IsLoggedIn(Params.LocalAccountId))
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::CheckUpdateSessionState] Could not update session with user [%s] not logged in"), *ToLogString(Params.LocalAccountId));

					return Errors::InvalidUser();
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckUpdateSessionState] Could not update session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return GetSessionByNameResult.GetErrorValue();
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
		if (!Services.GetAuthInterface()->IsLoggedIn(Params.LocalAccountId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckFindSessionsState] Could not find sessions with user [%s] not logged in"), *ToLogString(Params.LocalAccountId));

			return Errors::InvalidUser();
		}

		// Ongoing search check
		if (const TSharedRef<TOnlineAsyncOp<FFindSessions>>* CurrentSessionSearchHandle = CurrentSessionSearchHandlesUserMap.Find(Params.LocalAccountId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckFindSessionsState] Could not find sessions, search already in progress"));

			return Errors::AlreadyPending();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckStartMatchmakingParams(const FStartMatchmaking::Params& Params)
	{
		if (Params.SessionSettings.NumMaxPrivateConnections == 0 && Params.SessionSettings.NumMaxPublicConnections == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckStartMatchmakingParams] Could not create session with invalid NumMaxPrivateConnections and NumMaxPublicConnections"));

			return Errors::InvalidParams();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckStartMatchmakingState(const FStartMatchmaking::Params& Params)
	{
		// Check if a session with that name already exists
		TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.LocalAccountId, Params.SessionName);
		if (!Result.IsSet()) // If CheckSessionExistsByName did not return an error, a session with that name already exists
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckStartMatchmakingState] Could not join session with name [%s]. A session with that name already exists"), *Params.SessionName.ToString());

			return Errors::InvalidState(); // TODO: New error: Session with name %s already exists
		}

		// User login check for all local users
		IAuthPtr Auth = Services.GetAuthInterface();

		TArray<FAccountId> LocalAccountIds;
		LocalAccountIds.Reserve(Params.LocalAccounts.Num());
		Params.LocalAccounts.GenerateKeyArray(LocalAccountIds);

		for (const FAccountId& LocalAccountId : LocalAccountIds)
		{
			if (!Auth->IsLoggedIn(LocalAccountId))
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckStartMatchmakingState] Could not join session with user [%s] not logged in"), *ToLogString(LocalAccountId));

				return Errors::InvalidUser();
			}
		}

		// TODO: Check that only one session has bUsesPresence set

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckJoinSessionParams(const FJoinSession::Params& Params)
	{
		if (!Params.LocalAccountId.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session with name [%s]. LocalAccountId [%s] not valid"), *ToLogString(Params.LocalAccountId));

			return Errors::InvalidParams();
		}

		if (!Params.LocalAccounts.Contains(Params.LocalAccountId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session with name [%s]. Missing Session Member info for user [%s]"), *ToLogString(Params.LocalAccountId));

			return Errors::InvalidParams();
		}

		if (!Params.SessionId.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session with name [%s]. SessionId [%s] not valid"), *ToLogString(Params.SessionId));

			return Errors::InvalidParams();
		}

		if (Params.SessionName.IsNone())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionParams] Could not join session with no valid SessionName set"));

			return Errors::InvalidParams();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckJoinSessionState(const FJoinSession::Params& Params)
	{
		// Check if a session with that name already exists
		TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.LocalAccountId, Params.SessionName);
		if (!Result.IsSet()) // If CheckSessionExistsByName did not return an error, a session with that name already exists
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session with name [%s]. A session with that name already exists"), *Params.SessionName.ToString());

			return Errors::InvalidState(); // TODO: New error: Session with name %s already exists
		}

		// User login check for all local users
		IAuthPtr Auth = Services.GetAuthInterface();

		TArray<FAccountId> LocalAccountIds;
		LocalAccountIds.Reserve(Params.LocalAccounts.Num());
		Params.LocalAccounts.GenerateKeyArray(LocalAccountIds);

		for (const FAccountId& LocalAccountId : LocalAccountIds)
		{
			if(!Auth->IsLoggedIn(LocalAccountId))
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session with user [%s] not logged in"), *ToLogString(LocalAccountId));

				return Errors::InvalidUser();
			}
		}

		// We check that the session is cached and valid for a join operation by the users
		TOnlineResult<FGetSessionById> GetSessionByIdResult = GetSessionById({ Params.LocalAccountId, Params.SessionId });
		if (GetSessionByIdResult.IsError())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Unable to find session with SessionId parameter [%s]. Please call FindSessions to get an updated list of available sessions "), *ToLogString(Params.SessionId));

			return GetSessionByIdResult.GetErrorValue();
		}

		TSharedRef<const ISession> FoundSession = GetSessionByIdResult.GetOkValue().Session;

		const FSessionSettings& SessionSettings = FoundSession->GetSessionSettings();

		for (const FAccountId& LocalAccountId : LocalAccountIds)
		{
			if (SessionSettings.SessionMembers.Contains(LocalAccountId))
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session. User [%s] already in session"), *ToLogString(LocalAccountId));

				return Errors::AccessDenied();
			}
		}

		if (!SessionSettings.bAllowNewMembers)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session. bAllowNewMembers is set to false"));

			return Errors::AccessDenied();
		}

		if (!SessionSettings.bAllowUnregisteredPlayers)
		{
			for (const FAccountId& LocalAccountId : LocalAccountIds)
			{
				if (!SessionSettings.RegisteredPlayers.Contains(LocalAccountId))
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session. bAllowUnregisteredPlayers is set to false and user [%s] is not registered"), *ToLogString(LocalAccountId));

					return Errors::AccessDenied();
				}
			}
		}

		if (SessionSettings.NumMaxPublicConnections > 0 && SessionSettings.NumOpenPublicConnections < (uint32)LocalAccountIds.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session. Not enough NumOpenPublicConnections [%u] for the amount of joining players [%d]"), SessionSettings.NumOpenPublicConnections, LocalAccountIds.Num());

			return Errors::AccessDenied();
		}

		if (SessionSettings.NumMaxPrivateConnections > 0 && SessionSettings.NumOpenPrivateConnections < (uint32)LocalAccountIds.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session. Not enough NumOpenPrivateConnections [%u] for the amount of joining players [%d]"), SessionSettings.NumOpenPrivateConnections, LocalAccountIds.Num());

			return Errors::AccessDenied();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckAddSessionMembersState(const FAddSessionMembers::Params& Params)
	{
		if (TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.LocalAccountId, Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckAddSessionMembersState] Could not add session member to session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Result.GetValue();
		}

		// TODO: Check if there are enough slots available if Params::bReserveSlot is true

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckRemoveSessionMembersState(const FRemoveSessionMembers::Params& Params)
	{
		if (TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.LocalAccountId, Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckRemoveSessionMembersState] Could not remove session member from session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Result.GetValue();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckLeaveSessionState(const FLeaveSession::Params& Params)
	{
		// User login check for main caller, session check
		if (TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.LocalAccountId, Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckLeaveSessionState] Could not leave session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Result.GetValue();
		}

		IAuthPtr Auth = Services.GetAuthInterface();

		// User login check for all local users
		for (const FAccountId& LocalAccountId : Params.LocalAccounts)
		{
			if (!Auth->IsLoggedIn(LocalAccountId))
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckLeaveSessionState] Could not leave session with user [%s] not logged in"), *ToLogString(LocalAccountId));

				return Errors::InvalidUser();
			}
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckSendSessionInviteState(const FSendSessionInvite::Params& Params)
	{
		// User login check for main caller, session check
		TOnlineResult<FGetSessionByName> GetSessionByNameResult = GetSessionByName({ Params.SessionName });
		if (GetSessionByNameResult.IsOk())
		{
			TSharedRef<const ISession> FoundSession = GetSessionByNameResult.GetOkValue().Session;

			if (!FoundSession->GetSessionSettings().bIsDedicatedServerSession)
			{
				if (!Services.GetAuthInterface()->IsLoggedIn(Params.LocalAccountId))
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::CheckSendSessionInviteState] Could not send session invite with user [%s] not logged in"), *ToLogString(Params.LocalAccountId));

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
		if (!Services.GetAuthInterface()->IsLoggedIn(Params.LocalAccountId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::CheckRejectSessionInviteState] Could not send session invite with user [%s] not logged in"), *ToLogString(Params.LocalAccountId));

			return Errors::InvalidUser();
		}

		// TODO: check if Session Invite Id is valid

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckRegisterPlayersState(const FRegisterPlayers::Params& Params)
	{
		if (TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.LocalAccountId, Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckRegisterPlayersState] Could not register players in session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Result.GetValue();
		}

		// TODO: Check if there are enough slots available if Params::bReserveSlot is true

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckUnregisterPlayersState(const FUnregisterPlayers::Params& Params)
	{
		if(TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.LocalAccountId, Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckUnregisterPlayersState] Could not unregister players from session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Result.GetValue();
		}

		return Errors::Success();
	}

	// TODO: Have all Check methods return TOptional too, change call sites, and write Macro for repeating code structure
	TOptional<FOnlineError> FSessionsCommon::CheckSessionExistsByName(const FAccountId& LocalAccountId, const FName& SessionName)
	{
		TOptional<FOnlineError> Result;

		TOnlineResult<FGetSessionByName> GetSessionByNameResult = GetSessionByName({ SessionName });
		if (GetSessionByNameResult.IsError())
		{
			Result.Emplace(GetSessionByNameResult.GetErrorValue());
		}

		return Result;
	}

/* UE::Online */ }
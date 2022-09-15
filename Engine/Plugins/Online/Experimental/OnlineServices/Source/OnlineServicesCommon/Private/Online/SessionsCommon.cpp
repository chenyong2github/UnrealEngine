// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsCommon.h"

#include "Online/Auth.h"
#include "Online/OnlineServicesCommon.h"

namespace UE::Online {
	/** FSessionCommon */

	FSessionCommon& FSessionCommon::operator+=(const FSessionUpdate& SessionUpdate)
	{
		if (SessionUpdate.OwnerAccountId.IsSet())
		{
			OwnerAccountId = SessionUpdate.OwnerAccountId.GetValue();
		}

		if (SessionUpdate.SessionSettingsChanges.IsSet())
		{
			SessionSettings += SessionUpdate.SessionSettingsChanges.GetValue();
		}

		// Session Members

		for (const FAccountId& Key : SessionUpdate.RemovedSessionMembers)
		{
			SessionMembers.Remove(Key);
		}

		SessionMembers.Append(SessionUpdate.AddedSessionMembers);

		return *this;
	}

	FString FSessionCommon::ToLogString() const
	{
		return FString::Printf(TEXT("ISession: SessionId [%s]"), *UE::Online::ToLogString(SessionInfo.SessionId));
	}

	void FSessionCommon::DumpState() const
	{
		UE_LOG(LogTemp, Log, TEXT("[ISession]"));
		DumpMemberData();
		DumpSessionInfo();
		DumpSessionSettings();
	}

	void FSessionCommon::DumpMemberData() const
	{
		UE_LOG(LogTemp, Log, TEXT("OwnerAccountId [%s]"), *UE::Online::ToLogString(OwnerAccountId));
		UE_LOG(LogTemp, Log, TEXT("SessionMemberIdsSet:"));

		for (const FAccountId& SessionMemberId : SessionMembers)
		{
			UE_LOG(LogTemp, Log, TEXT("	[%s]"), *UE::Online::ToLogString(SessionMemberId));
		}
	}

	void FSessionCommon::DumpSessionInfo() const
	{
		UE_LOG(LogTemp, Log, TEXT("SessionInfo:"));
		UE_LOG(LogTemp, Log, TEXT("	SessionId [%s]"), *UE::Online::ToLogString(SessionInfo.SessionId));
		UE_LOG(LogTemp, Log, TEXT("	bAllowSanctionedPlayers [%d]"), SessionInfo.bAllowSanctionedPlayers);
		UE_LOG(LogTemp, Log, TEXT("	bAntiCheatProtected [%d]"), SessionInfo.bAntiCheatProtected);
		UE_LOG(LogTemp, Log, TEXT("	bIsDedicatedServerSession [%d]"), SessionInfo.bIsDedicatedServerSession);
		UE_LOG(LogTemp, Log, TEXT("	bIsLANSession [%d]"), SessionInfo.bIsLANSession);
		UE_LOG(LogTemp, Log, TEXT("	SessionIdOverride [%s]"), *SessionInfo.SessionIdOverride);
	}

	void FSessionCommon::DumpSessionSettings() const
	{
		UE_LOG(LogTemp, Log, TEXT("SessionSettings:"));
		UE_LOG(LogTemp, Log, TEXT("	bAllowNewMembers [%d]"), SessionSettings.bAllowNewMembers);
		UE_LOG(LogTemp, Log, TEXT("	JoinPolicy [%s]"), LexToString(SessionSettings.JoinPolicy));
		UE_LOG(LogTemp, Log, TEXT("	NumMaxConnections [%d]"), SessionSettings.NumMaxConnections);
		UE_LOG(LogTemp, Log, TEXT("	SchemaName [%s]"), *SessionSettings.SchemaName.ToString());
		UE_LOG(LogTemp, Log, TEXT("	CustomSettings:"));

		for (const TPair<FSchemaAttributeId, FCustomSessionSetting>& Entry : SessionSettings.CustomSettings)
		{
			UE_LOG(LogTemp, Log, TEXT("		Key: [%s]"), *Entry.Key.ToString());
			UE_LOG(LogTemp, Log, TEXT("		Value:	Visibility [%s]"), LexToString(Entry.Value.Visibility));
			UE_LOG(LogTemp, Log, TEXT("				ID [%d]"), Entry.Value.ID);
			UE_LOG(LogTemp, Log, TEXT("				Data [%s]"), *LexToString(Entry.Value.Data));
		}
	}

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
		RegisterCommand(&FSessionsCommon::GetPresenceSession);
		RegisterCommand(&FSessionsCommon::IsPresenceSession);
		RegisterCommand(&FSessionsCommon::SetPresenceSession);
		RegisterCommand(&FSessionsCommon::ClearPresenceSession);
		RegisterCommand(&FSessionsCommon::CreateSession);
		RegisterCommand(&FSessionsCommon::UpdateSessionSettings);
		RegisterCommand(&FSessionsCommon::LeaveSession);
		RegisterCommand(&FSessionsCommon::FindSessions);
		RegisterCommand(&FSessionsCommon::StartMatchmaking);
		RegisterCommand(&FSessionsCommon::JoinSession);
		RegisterCommand(&FSessionsCommon::AddSessionMember);
		RegisterCommand(&FSessionsCommon::RemoveSessionMember);
		RegisterCommand(&FSessionsCommon::SendSessionInvite);
		RegisterCommand(&FSessionsCommon::GetSessionInviteById);
		RegisterCommand(&FSessionsCommon::GetAllSessionInvites);
		RegisterCommand(&FSessionsCommon::RejectSessionInvite);
	}

	TOnlineResult<FGetAllSessions> FSessionsCommon::GetAllSessions(FGetAllSessions::Params&& Params) const
	{
		// TODO: Params and user login check

		TArray<TSharedRef<const ISession>> Sessions;

		if (const TArray<FName>* UserSessions = NamedSessionUserMap.Find(Params.LocalAccountId))
		{
			for (const FName& SessionName : *UserSessions)
			{
				const FOnlineSessionId& SessionId = LocalSessionsByName.FindChecked(SessionName);
				const TSharedRef<FSessionCommon>& Session = AllSessionsById.FindChecked(SessionId);

				Sessions.Add(Session);
			}
		}

		return TOnlineResult<FGetAllSessions>({ MoveTemp(Sessions) });
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
		TOnlineResult<FGetMutableSessionById> GetMutableSessionByIdResult = GetMutableSessionById({ Params.SessionId });
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
		// TODO: Check the user is valid and logged in

		if (const FOnlineSessionId* PresenceSessionId = PresenceSessionsUserMap.Find(Params.LocalAccountId))
		{
			return TOnlineResult<FGetPresenceSession>({ AllSessionsById.FindChecked(*PresenceSessionId) });
		}
		else
		{
			return TOnlineResult<FGetPresenceSession>({ Errors::InvalidState() });
		}
	}

	TOnlineResult<FIsPresenceSession> FSessionsCommon::IsPresenceSession(FIsPresenceSession::Params&& Params) const
	{
		// TODO: Check the user is valid and logged in. Check the session id is valid

		if (const FOnlineSessionId* PresenceSessionId = PresenceSessionsUserMap.Find(Params.LocalAccountId))
		{
			return TOnlineResult<FIsPresenceSession>(FIsPresenceSession::Result{ Params.SessionId == (*PresenceSessionId) });
		}
		else
		{
			return TOnlineResult<FIsPresenceSession>({ Errors::InvalidState() });
		}
	}

	TOnlineResult<FSetPresenceSession> FSessionsCommon::SetPresenceSession(FSetPresenceSession::Params&& Params)
	{
		// TODO: Check the user is valid and logged in. Check the session id is valid

		FOnlineSessionId& PresenceSessionId = PresenceSessionsUserMap.FindOrAdd(Params.LocalAccountId);
		PresenceSessionId = Params.SessionId;

		return TOnlineResult<FSetPresenceSession>(FSetPresenceSession::Result{ });
	}

	TOnlineResult<FClearPresenceSession> FSessionsCommon::ClearPresenceSession(FClearPresenceSession::Params&& Params)
	{
		// TODO: Check the user is valid and logged in

		PresenceSessionsUserMap.Remove(Params.LocalAccountId);

		return TOnlineResult<FClearPresenceSession>(FClearPresenceSession::Result{ });
	}

	TOnlineAsyncOpHandle<FCreateSession> FSessionsCommon::CreateSession(FCreateSession::Params&& Params)
	{
		TOnlineAsyncOpRef<FCreateSession> Operation = GetOp<FCreateSession>(MoveTemp(Params));
		Operation->SetError(Errors::NotImplemented());
		return Operation->GetHandle();
	}

	TOnlineAsyncOpHandle<FUpdateSessionSettings> FSessionsCommon::UpdateSessionSettings(FUpdateSessionSettings::Params&& Params)
	{
		TOnlineAsyncOpRef<FUpdateSessionSettings> Op = GetOp<FUpdateSessionSettings>(MoveTemp(Params));
		const FUpdateSessionSettings::Params& OpParams = Op->GetParams();

		FOnlineError ParamsCheck = CheckUpdateSessionSettingsParams(OpParams);
		if (ParamsCheck != Errors::Success())
		{
			Op->SetError(MoveTemp(ParamsCheck));
			return Op->GetHandle();
		}

		Op->Then([this](TOnlineAsyncOp<FUpdateSessionSettings>& Op) mutable
		{
			const FUpdateSessionSettings::Params& OpParams = Op.GetParams();

			FOnlineError StateCheck = CheckUpdateSessionSettingsState(OpParams);
			if (StateCheck != Errors::Success())
			{
				Op.SetError(MoveTemp(StateCheck));
				return;
			}

			UpdateSessionSettingsImpl({ OpParams.LocalAccountId, OpParams.SessionName, OpParams.Mutations })
			.Next([this, WeakOp = Op.AsWeak()](const TOnlineResult<FUpdateSessionSettingsImpl>& Result)
			{
				if (TOnlineAsyncOpPtr<FUpdateSessionSettings> StrongOp = WeakOp.Pin())
				{
					if (Result.IsOk())
					{
						StrongOp->SetResult({ });
					}
					else
					{
						FOnlineError ErrorValue = Result.GetErrorValue();
						StrongOp->SetError(MoveTemp(ErrorValue));
					}
				}
			});			
		})
		.Enqueue(GetSerialQueue());

		return Op->GetHandle();
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

	TOnlineAsyncOpHandle<FAddSessionMember> FSessionsCommon::AddSessionMember(FAddSessionMember::Params&& Params)
	{
		TOnlineAsyncOpRef<FAddSessionMember> Op = GetOp<FAddSessionMember>(MoveTemp(Params));

		Op->Then([this](TOnlineAsyncOp<FAddSessionMember>& Op) mutable
		{
			const FAddSessionMember::Params& OpParams = Op.GetParams();

			FOnlineError StateCheck = CheckAddSessionMemberState(OpParams);
			if (StateCheck != Errors::Success())
			{
				Op.SetError(MoveTemp(StateCheck));
				return;
			}

			TOnlineResult<FAddSessionMember> Result = AddSessionMemberImpl(OpParams);
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

	TOnlineAsyncOpHandle<FRemoveSessionMember> FSessionsCommon::RemoveSessionMember(FRemoveSessionMember::Params&& Params)
	{
		TOnlineAsyncOpRef<FRemoveSessionMember> Op = GetOp<FRemoveSessionMember>(MoveTemp(Params));

		Op->Then([this](TOnlineAsyncOp<FRemoveSessionMember>& Op) mutable
		{
			const FRemoveSessionMember::Params& OpParams = Op.GetParams();

			FOnlineError StateCheck = CheckRemoveSessionMemberState(OpParams);
			if (StateCheck != Errors::Success())
			{
				Op.SetError(MoveTemp(StateCheck));
				return;
			}

			TOnlineResult<FRemoveSessionMember> Result = RemoveSessionMemberImpl(OpParams);
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

	TOnlineResult<FGetSessionInviteById> FSessionsCommon::GetSessionInviteById(FGetSessionInviteById::Params&& Params)
	{
		if (const TMap<FSessionInviteId, TSharedRef<FSessionInvite>>* UserMap = SessionInvitesUserMap.Find(Params.LocalAccountId))
		{
			if (const TSharedRef<FSessionInvite>* SessionInvite = UserMap->Find(Params.SessionInviteId))
			{
				return TOnlineResult<FGetSessionInviteById>({ *SessionInvite });
			}
			else
			{
				return TOnlineResult<FGetSessionInviteById>(Errors::NotFound());
			}
		}
		else
		{
			return TOnlineResult<FGetSessionInviteById>(Errors::InvalidState());
		}		
	}

	TOnlineResult<FGetAllSessionInvites> FSessionsCommon::GetAllSessionInvites(FGetAllSessionInvites::Params&& Params)
	{
		TArray<TSharedRef<const FSessionInvite>> SessionInvites;

		if (const TMap<FSessionInviteId, TSharedRef<FSessionInvite>>* UserMap = SessionInvitesUserMap.Find(Params.LocalAccountId))
		{
			for (const TPair<FSessionInviteId, TSharedRef<FSessionInvite>>& Entry : *UserMap)
			{
				SessionInvites.Add(Entry.Value);
			}
		}

		return TOnlineResult<FGetAllSessionInvites>({ MoveTemp(SessionInvites) });
	}

	TOnlineAsyncOpHandle<FRejectSessionInvite> FSessionsCommon::RejectSessionInvite(FRejectSessionInvite::Params&& Params)
	{
		TOnlineAsyncOpRef<FRejectSessionInvite> Operation = GetOp<FRejectSessionInvite>(MoveTemp(Params));
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
		// TODO: Check that name is not empty

		if (const FOnlineSessionId* SessionId = LocalSessionsByName.Find(Params.LocalName))
		{
			check(AllSessionsById.Contains(*SessionId));

			return TOnlineResult<FGetMutableSessionByName>({ AllSessionsById.FindChecked(*SessionId) });
		}
		else
		{
			return TOnlineResult<FGetMutableSessionByName>(Errors::NotFound());
		}
	}

	TOnlineResult<FGetMutableSessionById> FSessionsCommon::GetMutableSessionById(FGetMutableSessionById::Params&& Params) const
	{
		// TODO: Check that session id is valid

		if (const TSharedRef<FSessionCommon>* FoundSession = AllSessionsById.Find(Params.SessionId))
		{
			return TOnlineResult<FGetMutableSessionById>({ *FoundSession });
		}
		else
		{
			return TOnlineResult<FGetMutableSessionById>(Errors::NotFound());
		}
	}

	void FSessionsCommon::AddSessionInvite(const TSharedRef<FSessionInvite> SessionInvite, const TSharedRef<FSessionCommon> Session, const FAccountId& LocalAccountId)
	{
		AllSessionsById.Emplace(Session->GetSessionId(), Session);

		TMap<FSessionInviteId, TSharedRef<FSessionInvite>>& SessionInvitesMap = SessionInvitesUserMap.FindOrAdd(LocalAccountId);
		SessionInvitesMap.Emplace(SessionInvite->InviteId, SessionInvite);
	}

	void FSessionsCommon::AddSearchResult(const TSharedRef<FSessionCommon> Session, const FAccountId& LocalAccountId)
	{
		AllSessionsById.Emplace(Session->GetSessionId(), Session);

		TArray<FOnlineSessionId>& SearchResults = SearchResultsUserMap.FindOrAdd(LocalAccountId);
		SearchResults.Add(Session->GetSessionId());
	}

	void FSessionsCommon::AddSessionWithReferences(const TSharedRef<FSessionCommon> Session, const FName& SessionName, const FAccountId& LocalAccountId, bool bIsPresenceSession)
	{
		AllSessionsById.Emplace(Session->GetSessionId(), Session);

		AddSessionReferences(Session->GetSessionId(), SessionName, LocalAccountId, bIsPresenceSession);
	}

	void FSessionsCommon::AddSessionReferences(const FOnlineSessionId SessionId, const FName& SessionName, const FAccountId& LocalAccountId, bool bIsPresenceSession)
	{
		LocalSessionsByName.Emplace(SessionName, SessionId);

		NamedSessionUserMap.FindOrAdd(LocalAccountId).AddUnique(SessionName);

		if (bIsPresenceSession)
		{
			SetPresenceSession({ LocalAccountId, SessionId });
		}
	}

	void FSessionsCommon::ClearSessionInvitesForSession(const FAccountId& LocalAccountId, const FOnlineSessionId SessionId)
	{
		if (TMap<FSessionInviteId, TSharedRef<FSessionInvite>>* UserMap = SessionInvitesUserMap.Find(LocalAccountId))
		{
			TArray<FSessionInviteId> InviteIdsToRemove;
			for (const TPair<FSessionInviteId, TSharedRef<FSessionInvite>>& Entry : *UserMap)
			{
				if (Entry.Value->SessionId == SessionId)
				{
					InviteIdsToRemove.Add(Entry.Key);
				}
			}

			for (const FSessionInviteId& InviteId : InviteIdsToRemove)
			{
				UserMap->Remove(InviteId);
			}
		}
	}

	void FSessionsCommon::ClearSessionReferences(const FOnlineSessionId SessionId, const FName& SessionName, const FAccountId& LocalAccountId)
	{
		NamedSessionUserMap.FindChecked(LocalAccountId).Remove(SessionName);


		TOnlineResult<FIsPresenceSession> IsPresenceSessionResult = IsPresenceSession({ LocalAccountId, SessionId });
		if (IsPresenceSessionResult.IsOk())
		{
			if (IsPresenceSessionResult.GetOkValue().bIsPresenceSession)
			{
				ClearPresenceSession({ LocalAccountId });
			}
		}

		ClearSessionByName(SessionName);
		ClearSessionById(SessionId);
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

	void FSessionsCommon::ClearSessionById(const FOnlineSessionId& SessionId)
	{
		// PresenceSessionsUserMap is not evaluated, since any session there would also be in LocalSessionsByName
		for (const TPair<FName, FOnlineSessionId>& Entry : LocalSessionsByName)
		{
			if (Entry.Value == SessionId)
			{
				return;
			}
		}

		for (const TPair<FAccountId, TMap<FSessionInviteId, TSharedRef<FSessionInvite>>>& Entry : SessionInvitesUserMap)
		{
			for (const TPair<FSessionInviteId, TSharedRef<FSessionInvite>>& InviteMap : Entry.Value)
			{
				if (InviteMap.Value->SessionId == SessionId)
				{
					return;
				}
			}			
		}

		for (const TPair<FAccountId, TArray<FOnlineSessionId>>& Entry : SearchResultsUserMap)
		{
			if (Entry.Value.Contains(SessionId))
			{
				return;
			}
		}

		// If no references were found, we'll remove the session entry
		AllSessionsById.Remove(SessionId);
	}

#define COPY_TOPTIONAL_VALUE_IF_SET(Value) \
	if (UpdatedValues.Value.IsSet()) \
	{ \
		SessionSettingsChanges.Value = UpdatedValues.Value.GetValue(); \
	} \

	FSessionUpdate FSessionsCommon::BuildSessionUpdate(const TSharedRef<FSessionCommon>& Session, const FSessionSettingsUpdate& UpdatedValues) const
	{
		FSessionUpdate Result;

		FSessionSettingsChanges SessionSettingsChanges;

		COPY_TOPTIONAL_VALUE_IF_SET(SchemaName) // TODO: We may need some additional logic for schema changes
		COPY_TOPTIONAL_VALUE_IF_SET(NumMaxConnections)
		COPY_TOPTIONAL_VALUE_IF_SET(JoinPolicy)
		COPY_TOPTIONAL_VALUE_IF_SET(bAllowNewMembers)

		SessionSettingsChanges.RemovedCustomSettings.Append(UpdatedValues.RemovedCustomSettings);

		for (const TPair<FSchemaAttributeId, FCustomSessionSetting>& Entry : UpdatedValues.UpdatedCustomSettings)
		{
			if (const FCustomSessionSetting* CustomSetting = Session->GetSessionSettings().CustomSettings.Find(Entry.Key))
			{
				FCustomSessionSettingUpdate SettingUpdate = { *CustomSetting, Entry.Value };

				SessionSettingsChanges.ChangedCustomSettings.Emplace(Entry.Key, SettingUpdate);
			}
			else
			{
				SessionSettingsChanges.AddedCustomSettings.Add(Entry);
			}
		}

		Result.SessionSettingsChanges = SessionSettingsChanges;

		return Result;
	}

#undef COPY_TOPTIONAL_VALUE_IF_SET

	TFuture<TOnlineResult<FUpdateSessionSettingsImpl>> FSessionsCommon::UpdateSessionSettingsImpl(FUpdateSessionSettingsImpl::Params&& Params)
	{
		return MakeFulfilledPromise<TOnlineResult<FUpdateSessionSettingsImpl>>(Errors::NotImplemented()).GetFuture();
	}

	TOnlineResult<FAddSessionMember> FSessionsCommon::AddSessionMemberImpl(const FAddSessionMember::Params& Params)
	{
		TOnlineResult<FGetMutableSessionByName> GetMutableSessionByNameResult = GetMutableSessionByName({ Params.SessionName });
		if (GetMutableSessionByNameResult.IsError())
		{
			return TOnlineResult<FAddSessionMember>(GetMutableSessionByNameResult.GetErrorValue());
		}
		TSharedRef<FSessionCommon> FoundSession = GetMutableSessionByNameResult.GetOkValue().Session;

		FSessionSettings& SessionSettings = FoundSession->SessionSettings;

		if (FoundSession->GetNumOpenConnections() <= 0)
		{
			return TOnlineResult<FAddSessionMember>(Errors::InvalidState());
		}

		FoundSession->SessionMembers.Emplace(Params.LocalAccountId);

		return TOnlineResult<FAddSessionMember>();
	}

	TOnlineResult<FRemoveSessionMember> FSessionsCommon::RemoveSessionMemberImpl(const FRemoveSessionMember::Params& Params)
	{
		TOnlineResult<FGetMutableSessionByName> GetMutableSessionByNameResult = GetMutableSessionByName({ Params.SessionName });
		if (GetMutableSessionByNameResult.IsError())
		{
			return TOnlineResult<FRemoveSessionMember>(GetMutableSessionByNameResult.GetErrorValue());
		}
		TSharedRef<FSessionCommon> FoundSession = GetMutableSessionByNameResult.GetOkValue().Session;

		FSessionSettings& SessionSettings = FoundSession->SessionSettings;
		
		if (FoundSession->GetNumOpenConnections() == SessionSettings.NumMaxConnections)
		{
			return TOnlineResult<FRemoveSessionMember>(Errors::InvalidState());
		}

		FoundSession->SessionMembers.Remove(Params.LocalAccountId);

		return TOnlineResult<FRemoveSessionMember>();
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

		if (Params.SessionSettings.NumMaxConnections == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionParams] Could not create session with name [%s] with no valid NumMaxConnections [%d]"), *Params.SessionName.ToString(), Params.SessionSettings.NumMaxConnections);

			return Errors::InvalidParams();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckCreateSessionState(const FCreateSession::Params& Params)
	{
		TOnlineResult<FGetSessionByName> GetSessionByNameResult = GetSessionByName({ Params.SessionName });
		if (GetSessionByNameResult.IsOk())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionState] Could not create session with name [%s]. A session with that name already exists"), *Params.SessionName.ToString());

			return Errors::InvalidState();
		}

		if (Params.bPresenceEnabled)
		{
			for (const TPair<FName, FOnlineSessionId>& Entry : LocalSessionsByName)
			{
				TOnlineResult<FGetPresenceSession> GetPresenceSessionResult = GetPresenceSession({ Params.LocalAccountId });
				if (GetPresenceSessionResult.IsOk())
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionState] Could not create session with bPresenceEnabled set to true when another already exists [%s]."), *Entry.Key.ToString());

					return Errors::InvalidState();
				}
			}
		}

		// User login check for all local users
		IAuthPtr Auth = Services.GetAuthInterface();
		if (!Auth->IsLoggedIn(Params.LocalAccountId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckCreateSessionState] Could not create session with user [%s] not logged in"), *ToLogString(Params.LocalAccountId));

			return Errors::InvalidUser();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckUpdateSessionSettingsParams(const FUpdateSessionSettings::Params& Params)
	{
		if (!Params.LocalAccountId.IsValid())
		{
			return Errors::InvalidUser();
		}

		if (Params.SessionName.IsNone())
		{
			return Errors::InvalidParams();
		}

		for (const TPair<FSchemaAttributeId, FCustomSessionSetting>& Entry : Params.Mutations.UpdatedCustomSettings)
		{
			if (Entry.Key.IsNone())
			{
				return Errors::InvalidParams();
			}
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckUpdateSessionSettingsState(const FUpdateSessionSettings::Params& Params)
	{
		// User login check
		IAuthPtr Auth = Services.GetAuthInterface();
		if (!Auth->IsLoggedIn(Params.LocalAccountId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckUpdateSessionSettingsState] User [%s] not logged in"), *ToLogString(Params.LocalAccountId));

			return Errors::InvalidUser();
		}

		// Session name check
		if (TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.LocalAccountId, Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckUpdateSessionSettingsState] Session with name [%s] not found."), *Params.SessionName.ToString());

			return Result.GetValue();
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
		if (Params.SessionCreationParameters.SessionSettings.NumMaxConnections == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckStartMatchmakingParams] Could not create session with invalid NumMaxPrivateConnections and NumMaxPublicConnections"));

			return Errors::InvalidParams();
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckStartMatchmakingState(const FStartMatchmaking::Params& Params)
	{
		// Check if a session with that name already exists
		TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.SessionCreationParameters.LocalAccountId, Params.SessionCreationParameters.SessionName);
		if (!Result.IsSet()) // If CheckSessionExistsByName did not return an error, a session with that name already exists
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckStartMatchmakingState] Could not join session with name [%s]. A session with that name already exists"), *Params.SessionCreationParameters.SessionName.ToString());

			return Errors::InvalidState(); // TODO: New error: Session with name %s already exists
		}

		// User login check for all local users
		IAuthPtr Auth = Services.GetAuthInterface();
		if (!Auth->IsLoggedIn(Params.SessionCreationParameters.LocalAccountId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckStartMatchmakingState] Could not join session with user [%s] not logged in"), *ToLogString(Params.SessionCreationParameters.LocalAccountId));

			return Errors::InvalidUser();
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
		if(!Auth->IsLoggedIn(Params.LocalAccountId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session with user [%s] not logged in"), *ToLogString(Params.LocalAccountId));

			return Errors::InvalidUser();
		}

		// We check that the session is cached and valid for a join operation by the users
		TOnlineResult<FGetSessionById> GetSessionByIdResult = GetSessionById({ Params.SessionId });
		if (GetSessionByIdResult.IsError())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Unable to find session with SessionId parameter [%s]. Please call FindSessions to get an updated list of available sessions "), *ToLogString(Params.SessionId));

			return GetSessionByIdResult.GetErrorValue();
		}

		TSharedRef<const ISession> FoundSession = GetSessionByIdResult.GetOkValue().Session;

		const FSessionSettings& SessionSettings = FoundSession->GetSessionSettings();

		if (FoundSession->GetSessionMembers().Contains(Params.LocalAccountId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session. User [%s] already in session"), *ToLogString(Params.LocalAccountId));

			return Errors::AccessDenied();
		}

		if (!FoundSession->IsJoinable())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not join session. Session not joinable "));

			return Errors::AccessDenied();
		}

		if (Params.bPresenceEnabled)
		{
			for (const TPair<FName, FOnlineSessionId>& Entry : LocalSessionsByName)
			{
				TOnlineResult<FGetPresenceSession> GetPresenceSessionResult = GetPresenceSession({ Params.LocalAccountId });
				if (GetPresenceSessionResult.IsOk())
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckJoinSessionState] Could not create session with bPresenceEnabled set to true when another already exists [%s]."), *Entry.Key.ToString());

					return Errors::InvalidState();
				}
			}
		}

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckAddSessionMemberState(const FAddSessionMember::Params& Params)
	{
		if (TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.LocalAccountId, Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckAddSessionMemberState] Could not add session member to session with name [%s]. Session not found"), *Params.SessionName.ToString());

			return Result.GetValue();
		}

		// TODO: Check if there are enough slots available if Params::bReserveSlot is true

		return Errors::Success();
	}

	FOnlineError FSessionsCommon::CheckRemoveSessionMemberState(const FRemoveSessionMember::Params& Params)
	{
		if (TOptional<FOnlineError> Result = CheckSessionExistsByName(Params.LocalAccountId, Params.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckRemoveSessionMemberState] Could not remove session member from session with name [%s]. Session not found"), *Params.SessionName.ToString());

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

		// User login check
		IAuthPtr Auth = Services.GetAuthInterface();
		if (!Auth->IsLoggedIn(Params.LocalAccountId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsCommon::CheckLeaveSessionState] Could not leave session with user [%s] not logged in"), *ToLogString(Params.LocalAccountId));

			return Errors::InvalidUser();
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

			if (!FoundSession->GetSessionInfo().bIsDedicatedServerSession)
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
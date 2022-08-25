// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsOSSAdapter.h"

#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"

#include "Online/OnlineServicesOSSAdapter.h"
#include "Online/AuthOSSAdapter.h"
#include "Online/UserInfoOSSAdapter.h"
#include "Online/DelegateAdapter.h"

#include "Algo/Find.h"

namespace UE::Online {

/** Auxiliary functions */

EOnlineComparisonOp::Type GetV1SessionSearchComparisonOp(const ESchemaAttributeComparisonOp& InValue)
{
	switch (InValue)
	{
	case ESchemaAttributeComparisonOp::Equals:			return EOnlineComparisonOp::Equals;
	case ESchemaAttributeComparisonOp::GreaterThan:		return EOnlineComparisonOp::GreaterThan;
	case ESchemaAttributeComparisonOp::GreaterThanEquals:	return EOnlineComparisonOp::GreaterThanEquals;
	case ESchemaAttributeComparisonOp::LessThan:			return EOnlineComparisonOp::LessThan;
	case ESchemaAttributeComparisonOp::LessThanEquals:	return EOnlineComparisonOp::LessThanEquals;
	case ESchemaAttributeComparisonOp::Near:				return EOnlineComparisonOp::Near;
	case ESchemaAttributeComparisonOp::NotEquals:			return EOnlineComparisonOp::NotEquals;
	}

	checkNoEntry();

	return EOnlineComparisonOp::Equals;
}

FSchemaVariant GetV2SessionVariant(const FVariantData& InValue)
{
	FSchemaVariant Result;
	
	switch(InValue.GetType())
	{
	case EOnlineKeyValuePairDataType::Int32:
	{
		int32 Value;
		InValue.GetValue(Value);
		Result.Set((int64)Value);
		break;
	}
	case EOnlineKeyValuePairDataType::UInt32:
	{
		uint32 Value;
		InValue.GetValue(Value);
		Result.Set((int64)Value);
		break;
	}
	case EOnlineKeyValuePairDataType::Int64:
	{
		int64 Value;
		InValue.GetValue(Value);
		Result.Set(Value);
		break;
	}
	case EOnlineKeyValuePairDataType::UInt64:
	{
		uint64 Value;
		InValue.GetValue(Value);
		Result.Set((int64)Value);
		break;
	}
	case EOnlineKeyValuePairDataType::Float:
	{
		float Value;
		InValue.GetValue(Value);
		Result.Set((double)Value);
		break;
	}	
	case EOnlineKeyValuePairDataType::Double:
	{
		double Value;
		InValue.GetValue(Value);
		Result.Set(Value);
		break;
	}
	case EOnlineKeyValuePairDataType::String:
	{
		FString Value;
		InValue.GetValue(Value);
		Result.Set(Value);
		break;
	}
	case EOnlineKeyValuePairDataType::Bool:
	{
		bool Value;
		InValue.GetValue(Value);
		Result.Set(Value);
		break;
	}
	case EOnlineKeyValuePairDataType::Empty: // Intentional fallthrough
	case EOnlineKeyValuePairDataType::Blob:	// Intentional fallthrough
	case EOnlineKeyValuePairDataType::Json:	// Intentional fallthrough
	case EOnlineKeyValuePairDataType::MAX:	// Intentional fallthrough
	default:
		UE_LOG(LogTemp, Warning, TEXT("[GetV2SessionVariant] FVariantData type not supported by FSessionsVariant. No value was set."));
		break;
	}

	return Result;
}

FVariantData GetV1VariantData(const FSchemaVariant& InValue)
{
	FVariantData Result;

	switch (InValue.VariantType)
	{
	case ESchemaAttributeType::Bool:
		Result.SetValue(InValue.GetBoolean());
		break;
	case ESchemaAttributeType::Double:
		Result.SetValue(InValue.GetDouble());
		break;
	case ESchemaAttributeType::Int64:
		Result.SetValue(InValue.GetInt64());
		break;
	case ESchemaAttributeType::String:
		Result.SetValue(InValue.GetString());
		break;
	default:
		UE_LOG(LogTemp, Warning, TEXT("[GetV1VariantData] FSchemaVariant type not supported by FVariantData. No value was set."));
		break;
	}

	return Result;
}

ESchemaAttributeVisibility GetV2SessionsAttributeVisibility(const EOnlineDataAdvertisementType::Type& InValue)
{
	switch (InValue)
	{
	case EOnlineDataAdvertisementType::DontAdvertise:			// Intentional fallthrough
	case EOnlineDataAdvertisementType::ViaPingOnly:				return ESchemaAttributeVisibility::Private;
	case EOnlineDataAdvertisementType::ViaOnlineService:		// Intentional fallthrough
	case EOnlineDataAdvertisementType::ViaOnlineServiceAndPing:	return ESchemaAttributeVisibility::Public;
	}

	checkNoEntry();
	return ESchemaAttributeVisibility::Private;
}

EOnlineDataAdvertisementType::Type GetV1OnlineDataAdvertisementType(const ESchemaAttributeVisibility& InValue)
{
	switch (InValue)
	{
	case ESchemaAttributeVisibility::Private:	return EOnlineDataAdvertisementType::DontAdvertise;
	case ESchemaAttributeVisibility::Public:	return EOnlineDataAdvertisementType::ViaOnlineServiceAndPing;
	}

	checkNoEntry();
	return EOnlineDataAdvertisementType::DontAdvertise;
}

FCustomSessionSettingsMap GetV2SessionSettings(const ::FSessionSettings& InSessionSettings)
{
	FCustomSessionSettingsMap Result;

	for (const TPair<FName, FOnlineSessionSetting>& Entry : InSessionSettings)
	{
		FCustomSessionSetting NewCustomSetting;
		NewCustomSetting.Data = GetV2SessionVariant(Entry.Value.Data);
		NewCustomSetting.Visibility = GetV2SessionsAttributeVisibility(Entry.Value.AdvertisementType);
		NewCustomSetting.ID = Entry.Value.ID;

		Result.Emplace(Entry.Key, NewCustomSetting);
	}

	return Result;
}

::FSessionSettings GetV1SessionSettings(const FCustomSessionSettingsMap& InSessionSettings)
{
	::FSessionSettings Result;

	for (const TPair<FSchemaAttributeId, FCustomSessionSetting>& Entry : InSessionSettings)
	{
		FOnlineSessionSetting NewSessionSetting;
		NewSessionSetting.Data = GetV1VariantData(Entry.Value.Data);
		NewSessionSetting.AdvertisementType = GetV1OnlineDataAdvertisementType(Entry.Value.Visibility);
		NewSessionSetting.ID = Entry.Value.ID;

		Result.Add(Entry.Key, NewSessionSetting);
	}

	return Result;
}

TSharedRef<FOnlineSessionSearch> BuildV1SessionSearch(const FFindSessions::Params& SearchParams)
{
	TSharedRef<FOnlineSessionSearch> Result = MakeShared<FOnlineSessionSearch>();

	Result->bIsLanQuery = SearchParams.bFindLANSessions;

	Result->MaxSearchResults = SearchParams.MaxResults;

	if (const FFindSessionsSearchFilter* PingBucketSize = Algo::FindBy(SearchParams.Filters, OSS_ADAPTER_SESSION_SEARCH_PING_BUCKET_SIZE, &FFindSessionsSearchFilter::Key))
	{
		Result->PingBucketSize = (int32)PingBucketSize->Value.GetInt64();
	}

	if (const FFindSessionsSearchFilter* PlatformHash = Algo::FindBy(SearchParams.Filters, OSS_ADAPTER_SESSION_SEARCH_PLATFORM_HASH, &FFindSessionsSearchFilter::Key))
	{
		Result->PlatformHash = (int32)PlatformHash->Value.GetInt64();
	}

	for (const FFindSessionsSearchFilter& SearchFilter : SearchParams.Filters)
	{
		switch (SearchFilter.Value.VariantType)
		{
		case ESchemaAttributeType::Bool:
			Result->QuerySettings.Set<bool>(SearchFilter.Key, SearchFilter.Value.GetBoolean(), GetV1SessionSearchComparisonOp(SearchFilter.ComparisonOp));
			break;
		case ESchemaAttributeType::Double:
			Result->QuerySettings.Set<double>(SearchFilter.Key, SearchFilter.Value.GetDouble(), GetV1SessionSearchComparisonOp(SearchFilter.ComparisonOp));
			break;
		case ESchemaAttributeType::Int64:
			Result->QuerySettings.Set<uint64>(SearchFilter.Key, (uint64)SearchFilter.Value.GetInt64(), GetV1SessionSearchComparisonOp(SearchFilter.ComparisonOp));
			break;
		case ESchemaAttributeType::String:
			Result->QuerySettings.Set<FString>(SearchFilter.Key, SearchFilter.Value.GetString(), GetV1SessionSearchComparisonOp(SearchFilter.ComparisonOp));
			break;
		}
	}

	return Result;
}

/** FSessionOSSAdapter */

FSessionOSSAdapter::FSessionOSSAdapter(const FOnlineSession& InSession)
	: V1Session(InSession)
{

}

const FSessionOSSAdapter& FSessionOSSAdapter::Cast(const ISession& InSession)
{
	// TODO: Check for ::Platform type if that's the Adapter type

	return static_cast<const FSessionOSSAdapter&>(InSession);
}

const FOnlineSession& FSessionOSSAdapter::GetV1Session() const
{
	return V1Session;
}

/** FSessionsOSSAdapter */ 

FSessionsOSSAdapter::FSessionsOSSAdapter(FOnlineServicesOSSAdapter& InServices)
	: Super(InServices)
{
}

void FSessionsOSSAdapter::Initialize()
{
	Super::Initialize();

	IOnlineSubsystem& SubsystemV1 = static_cast<FOnlineServicesOSSAdapter&>(Services).GetSubsystem();

	SessionsInterface = SubsystemV1.GetSessionInterface();
	check(SessionsInterface);

	IdentityInterface = SubsystemV1.GetIdentityInterface();
	check(IdentityInterface);

	SessionsInterface->AddOnSessionInviteReceivedDelegate_Handle(FOnSessionInviteReceivedDelegate::CreateLambda([this](const FUniqueNetId& UserId, const FUniqueNetId& FromId, const FString& AppId, const FOnlineSessionSearchResult& InviteResult)
	{
		FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

		TSharedRef<FSessionCommon> Session = BuildV2Session(&InviteResult.Session);

		TSharedRef<FSessionInvite> SessionInvite = MakeShared<FSessionInvite>();
		SessionInvite->SenderId = ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(FromId.AsShared());
		SessionInvite->RecipientId = ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(UserId.AsShared());
		SessionInvite->InviteId = GetSessionInviteIdRegistry().BasicRegistry.FindOrAddHandle(AppId);
		SessionInvite->SessionId = Session->SessionId;

		SessionInvitesUserMap.FindOrAdd(SessionInvite->RecipientId).Emplace(SessionInvite->InviteId, SessionInvite);

		AllSessionsById.Emplace(Session->SessionId, Session);

		FSessionInviteReceived Event{ ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(UserId.AsShared()), SessionInvite };

		SessionEvents.OnSessionInviteReceived.Broadcast(Event);
	}));

	SessionsInterface->AddOnSessionUserInviteAcceptedDelegate_Handle(FOnSessionUserInviteAcceptedDelegate::CreateLambda([this](const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr InvitedUserIdPtr, const FOnlineSessionSearchResult& InviteResult)
	{
		FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

		FUniqueNetIdRef LocalUserId = FUniqueNetIdString::EmptyId();
		FUniqueNetIdPtr LocalUserIdPtr = IdentityInterface->GetUniquePlayerId(ControllerId);
		if (LocalUserIdPtr.IsValid())
		{
			LocalUserId = (*LocalUserIdPtr).AsShared();
		}

		FUISessionJoinRequested Event {
			ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(LocalUserId),
			TResult<FOnlineSessionIdHandle, FOnlineError>(GetSessionIdRegistry().FindOrAddHandle(InviteResult.Session.SessionInfo->GetSessionId().AsShared())),
			EUISessionJoinRequestedSource::FromInvitation
		};

		SessionEvents.OnUISessionJoinRequested.Broadcast(Event);
	}));

	SessionsInterface->AddOnSessionParticipantRemovedDelegate_Handle(FOnSessionParticipantRemovedDelegate::CreateLambda([this](FName SessionName, const FUniqueNetId& TargetUniqueNetId)
	{
			const FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);
			const FUniqueNetIdRef TargetUniqueNetIdRef = TargetUniqueNetId.AsShared();
			const FAccountId TargetAccountId = ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(TargetUniqueNetIdRef);

			FSessionSettingsUpdate SettingsUpdate;
			SettingsUpdate.RemovedSessionMembers.Add(TargetAccountId);

		const FSessionUpdated Event { SessionName , SettingsUpdate };

			SessionEvents.OnSessionUpdated.Broadcast(Event);
	}));

	SessionsInterface->AddOnSessionParticipantsChangeDelegate_Handle(FOnSessionParticipantsChangeDelegate::CreateLambda([this](FName SessionName, const FUniqueNetId& TargetUniqueNetId, bool bJoined)
	{
		// We won't transmit events for a session or member that doesn't exist
		const TOnlineResult<FGetSessionByName> GetSessionByNameResult = GetSessionByName({ SessionName });
		if (GetSessionByNameResult.IsOk())
		{
			const TSharedRef<const ISession>& FoundSession = GetSessionByNameResult.GetOkValue().Session;

			const FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);
			const FUniqueNetIdRef TargetUniqueNetIdRef = TargetUniqueNetId.AsShared();
			const FAccountId TargetAccountId = ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(TargetUniqueNetIdRef);

			FSessionSettingsUpdate SettingsUpdate;
			if (bJoined)
			{
				if (const FSessionMember* SessionMember = FoundSession->GetSessionSettings().SessionMembers.Find(TargetAccountId))
				{
					FSessionMemberUpdate SessionMemberUpdate;
					SessionMemberUpdate.UpdatedMemberSettings.Append(SessionMember->MemberSettings);

					SettingsUpdate.UpdatedSessionMembers.Emplace(TargetAccountId, SessionMemberUpdate);
				}
			}
			else
			{
				SettingsUpdate.RemovedSessionMembers.Add(TargetAccountId);
			}

			const FSessionUpdated Event { SessionName , SettingsUpdate };

			SessionEvents.OnSessionUpdated.Broadcast(Event);
		}
	}));

	SessionsInterface->AddOnSessionParticipantSettingsUpdatedDelegate_Handle(FOnSessionParticipantSettingsUpdatedDelegate::CreateLambda([this](FName SessionName, const FUniqueNetId& TargetUniqueNetId, const FOnlineSessionSettings& UpdatedSettings)
	{
		// We won't transmit events for a session or member that doesn't exist
		const TOnlineResult<FGetSessionByName> GetSessionByNameResult = GetSessionByName({ SessionName });
		if (GetSessionByNameResult.IsOk())
		{
			const TSharedRef<const ISession>& FoundSession = GetSessionByNameResult.GetOkValue().Session;

			const FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);
			const FUniqueNetIdRef TargetUniqueNetIdRef = TargetUniqueNetId.AsShared();
			const FAccountId TargetAccountId = ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(TargetUniqueNetIdRef);

			FSessionSettingsUpdate SettingsUpdate;
			if (const FSessionMember* SessionMember = FoundSession->GetSessionSettings().SessionMembers.Find(TargetAccountId))
			{
				const ::FSessionSettings* MemberSettings = UpdatedSettings.MemberSettings.Find(TargetUniqueNetIdRef);
				const FCustomSessionSettingsMap UpdatedMemberSettings = GetV2SessionSettings(*MemberSettings);

				FSessionMemberUpdate SessionMemberUpdate;
				for (const TPair<FName, FCustomSessionSetting>& Entry : UpdatedMemberSettings)
				{
					if (const FCustomSessionSetting* Setting = SessionMember->MemberSettings.Find(Entry.Key))
					{
						if (Setting->Data != Entry.Value.Data || Setting->Visibility != Entry.Value.Visibility || Setting->ID != Entry.Value.ID)
						{
							SessionMemberUpdate.UpdatedMemberSettings.Add(Entry);
						}
					}
				}

				SettingsUpdate.UpdatedSessionMembers.Emplace(TargetAccountId, SessionMemberUpdate);
			}

			const FSessionUpdated Event{ SessionName , SettingsUpdate };

			SessionEvents.OnSessionUpdated.Broadcast(Event);
		}
	}));
}

void FSessionsOSSAdapter::Shutdown()
{
	SessionsInterface->ClearOnSessionInviteReceivedDelegates(this);
	SessionsInterface->ClearOnSessionUserInviteAcceptedDelegates(this);
	SessionsInterface->ClearOnSessionParticipantRemovedDelegates(this);
	SessionsInterface->ClearOnSessionParticipantsChangeDelegates(this);
	SessionsInterface->ClearOnSessionParticipantSettingsUpdatedDelegates(this);

	Super::Shutdown();
}

TOnlineAsyncOpHandle<FCreateSession> FSessionsOSSAdapter::CreateSession(FCreateSession::Params&& Params)
{
	TOnlineAsyncOpRef<FCreateSession> Op = GetOp<FCreateSession>(MoveTemp(Params));
	const FCreateSession::Params& OpParams = Op->GetParams();

	FOnlineError ParamsCheck = CheckCreateSessionParams(OpParams);
	if (ParamsCheck != Errors::Success())
	{
		Op->SetError(MoveTemp(ParamsCheck));
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FCreateSession>& Op)
	{
		const FCreateSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckCreateSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		// Dedicated servers can update sessions so we won't check if LocalAccountId is valid
		const FUniqueNetIdRef LocalUserId = Services.Get<FAuthOSSAdapter>()->GetUniqueNetId(OpParams.LocalAccountId).ToSharedRef();

		MakeMulticastAdapter(this, SessionsInterface->OnCreateSessionCompleteDelegates,
		[this, WeakOp = Op.AsWeak()](FName SessionName, const bool bWasSuccessful) mutable
		{
			if (TOnlineAsyncOpPtr<FCreateSession> Op = WeakOp.Pin())
			{
				if (!bWasSuccessful)
				{
					Op->SetError(Errors::Unknown());
					return;
				}

				const FCreateSession::Params& OpParams = Op->GetParams();

				const FNamedOnlineSession* V1Session = SessionsInterface->GetNamedSession(SessionName);

				TSharedRef<FSessionCommon> V2Session = BuildV2Session(V1Session);

				AllSessionsById.Emplace(V2Session->SessionId, V2Session);

				LocalSessionsByName.Emplace(OpParams.SessionName, V2Session->SessionId);

				NamedSessionUserMap.FindOrAdd(OpParams.LocalAccountId).AddUnique(OpParams.SessionName);

				Op->SetResult({ });
			}
		});

		SessionsInterface->CreateSession(*LocalUserId, OpParams.SessionName, BuildV1Settings(OpParams.SessionSettings));

		// TODO: Pending support for multiple local users
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FUpdateSession> FSessionsOSSAdapter::UpdateSession(FUpdateSession::Params&& Params)
{
	TOnlineAsyncOpRef<FUpdateSession> Op = GetOp<FUpdateSession>(MoveTemp(Params));
	const FUpdateSession::Params& OpParams = Op->GetParams();

	Op->Then([this](TOnlineAsyncOp<FUpdateSession>& Op)
	{
		const FUpdateSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckUpdateSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		MakeMulticastAdapter(this, SessionsInterface->OnUpdateSessionCompleteDelegates,
		[this, WeakOp = Op.AsWeak()](FName SessionName, const bool bWasSuccessful)
		{
			if (TOnlineAsyncOpPtr<FUpdateSession> Op = WeakOp.Pin())
			{
				if (!bWasSuccessful)
				{
					Op->SetError(Errors::Unknown());
					return;
				}

				const FUpdateSession::Params& OpParams = Op->GetParams();

				const TOnlineResult<FGetMutableSessionByName> GetMutableSessionByNameResult = GetMutableSessionByName({ SessionName });
				if (GetMutableSessionByNameResult.IsOk())
				{
					const TSharedRef<FSessionCommon>& FoundSession = GetMutableSessionByNameResult.GetOkValue().Session;

					FSessionSettings& UpdatedV2Settings = FoundSession->SessionSettings;
					UpdatedV2Settings += OpParams.Mutations;

					Op->SetResult({ });

					FSessionUpdated Event { SessionName, OpParams.Mutations };

					SessionEvents.OnSessionUpdated.Broadcast(Event);
				}
				else
				{
					Op->SetError(Errors::NotFound());
				}
			}
		});

		const TOnlineResult<FGetMutableSessionByName> GetMutableSessionByNameResult = GetMutableSessionByName({ OpParams.SessionName });
		check(GetMutableSessionByNameResult.IsOk());
		
		const TSharedRef<FSessionCommon>& FoundSession = GetMutableSessionByNameResult.GetOkValue().Session;

		// We will update a copy here, and wait until the operation has completed successfully to update our local data
		FSessionSettings UpdatedV2Settings = FoundSession->SessionSettings;
		UpdatedV2Settings += OpParams.Mutations;
		
		FOnlineSessionSettings UpdatedV1Settings = BuildV1Settings(UpdatedV2Settings);

		SessionsInterface->UpdateSession(OpParams.SessionName, UpdatedV1Settings);
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FLeaveSession> FSessionsOSSAdapter::LeaveSession(FLeaveSession::Params&& Params)
{
	TOnlineAsyncOpRef<FLeaveSession> Op = GetOp<FLeaveSession>(MoveTemp(Params));
	const FLeaveSession::Params& OpParams = Op->GetParams();

	Op->Then([this](TOnlineAsyncOp<FLeaveSession>& Op)
	{
		const FLeaveSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckLeaveSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		MakeMulticastAdapter(this, SessionsInterface->OnDestroySessionCompleteDelegates,
		[this, WeakOp = Op.AsWeak()](FName SessionName, const bool bWasSuccessful) mutable
		{
			if (TOnlineAsyncOpPtr<FLeaveSession> Op = WeakOp.Pin())
			{
				if (!bWasSuccessful)
				{
					Op->SetError(Errors::Unknown());
					SessionsInterface->ClearOnDestroySessionCompleteDelegates(this);
					return;
				}

				const FLeaveSession::Params& OpParams = Op->GetParams();

				TOnlineResult<FGetSessionByName> GetSessionByNameResult = GetSessionByName({ OpParams.SessionName });
				if (GetSessionByNameResult.IsOk())
				{
					TSharedRef<const ISession> FoundSession = GetSessionByNameResult.GetOkValue().Session;

					NamedSessionUserMap.FindChecked(OpParams.LocalAccountId).Remove(OpParams.SessionName);

					if (FoundSession->GetSessionSettings().bPresenceEnabled)
					{
						PresenceSessionsUserMap.Remove(OpParams.LocalAccountId);
					}

					ClearSessionByName(OpParams.SessionName);
					ClearSessionById(FoundSession->GetSessionId());
				}

				Op->SetResult({ });

				FSessionLeft SessionLeftEvent;
				SessionLeftEvent.LocalAccountIds.Append(Op->GetParams().LocalAccounts);

				SessionEvents.OnSessionLeft.Broadcast(SessionLeftEvent);
			}

			SessionsInterface->ClearOnDestroySessionCompleteDelegates(this);
		});

		SessionsInterface->DestroySession(OpParams.SessionName);

		// TODO: Pending support for multiple local users
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FFindSessions> FSessionsOSSAdapter::FindSessions(FFindSessions::Params&& Params)
{
	TOnlineAsyncOpRef<FFindSessions> Op = GetOp<FFindSessions>(MoveTemp(Params));
	const FFindSessions::Params& OpParams = Op->GetParams();

	FOnlineError ParamsCheck = CheckFindSessionsParams(OpParams);
	if (ParamsCheck != Errors::Success())
	{
		Op->SetError(MoveTemp(ParamsCheck));
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FFindSessions>& Op)
	{
		const FFindSessions::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckFindSessionsState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		const FUniqueNetIdRef LocalUserId = Services.Get<FAuthOSSAdapter>()->GetUniqueNetId(OpParams.LocalAccountId).ToSharedRef();
		if (!LocalUserId->IsValid())
		{
			Op.SetError(Errors::InvalidUser());
			return;
		}

		if (PendingV1SessionSearchesPerUser.Contains(OpParams.LocalAccountId))
		{
			Op.SetError(Errors::AlreadyPending());
			return;
		}

		// If there is a target user, we will use FindFriendSession
		if (OpParams.SessionId.IsSet())
		{
			if (const FUniqueNetIdPtr SessionIdPtr = GetSessionIdRegistry().GetIdValue(*OpParams.SessionId))
			{
				const FUniqueNetIdRef SessionIdRef = SessionIdPtr.ToSharedRef();

				MakeMulticastAdapter(this, SessionsInterface->OnFindSessionsCompleteDelegates,
				[this, WeakOp = Op.AsWeak()](bool bWasSuccessful)
				{
					if (TOnlineAsyncOpPtr<FFindSessions> Op = WeakOp.Pin())
					{
						if (bWasSuccessful)
						{
							TSharedRef<FOnlineSessionSearch>& PendingV1SessionSearch = PendingV1SessionSearchesPerUser.FindChecked(Op->GetParams().LocalAccountId);

							TArray<TSharedRef<FSessionCommon>> FoundSessions = BuildV2SessionSearchResults(PendingV1SessionSearch->SearchResults);

							TArray<FOnlineSessionIdHandle> SearchResults = SearchResultsUserMap.FindOrAdd(Op->GetParams().LocalAccountId);
							for (const TSharedRef<FSessionCommon>& FoundSession : FoundSessions)
							{
								AllSessionsById.Emplace(FoundSession->SessionId, FoundSession);
								SearchResults.Add(FoundSession->SessionId);
							}

							Op->SetResult({ SearchResults });
						}
						else
						{
							Op->SetError(Errors::Unknown());
						}

						PendingV1SessionSearchesPerUser.Remove(Op->GetParams().LocalAccountId);
					}
				});

				FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

				FUniqueNetIdRef TargetIdRef = FUniqueNetIdString::EmptyId();
				if (OpParams.TargetUser.IsSet())
				{
					TargetIdRef = ServicesOSSAdapter.GetAccountIdRegistry().GetIdValue(*OpParams.TargetUser).ToSharedRef();
				}

				PendingV1SessionSearchesPerUser.Emplace(OpParams.LocalAccountId, MakeShared<FOnlineSessionSearch>());

				SessionsInterface->FindSessionById(*LocalUserId, *SessionIdRef, *TargetIdRef, *MakeDelegateAdapter(this, [this, WeakOp = Op.AsWeak()](int32 LocalUserNum, bool bWasSuccessful, const FOnlineSessionSearchResult& SingleSearchResult) mutable
				{
					if (TOnlineAsyncOpPtr<FFindSessions> Op = WeakOp.Pin())
					{
						if (!bWasSuccessful)
						{
							Op->SetError(Errors::Unknown());
							return;
						}

						TSharedRef<FOnlineSessionSearch>& PendingV1SessionSearch = PendingV1SessionSearchesPerUser.FindChecked(Op->GetParams().LocalAccountId);

						PendingV1SessionSearch->SearchResults.Add(SingleSearchResult);
					}
				}));
			}
			else
			{
				Op.SetError(Errors::InvalidParams());
				return;
			}
		}
		else if (OpParams.TargetUser.IsSet())
		{
			int32 LocalUserNum = Services.Get<FAuthOSSAdapter>()->GetLocalUserNum(OpParams.LocalAccountId);

			MakeMulticastAdapter(this, SessionsInterface->OnFindFriendSessionCompleteDelegates[LocalUserNum],
			[this, WeakOp = Op.AsWeak()](int32 LocalUserId, bool bWasSuccessful, const TArray<FOnlineSessionSearchResult>& FriendSearchResults)
			{
				if (TOnlineAsyncOpPtr<FFindSessions> Op = WeakOp.Pin())
				{
					if (bWasSuccessful)
					{
						TArray<TSharedRef<FSessionCommon>> FoundSessions = BuildV2SessionSearchResults(FriendSearchResults);
						TArray<FOnlineSessionIdHandle> SearchResults = SearchResultsUserMap.FindOrAdd(Op->GetParams().LocalAccountId);
						for (const TSharedRef<FSessionCommon>& FoundSession : FoundSessions)
						{
							AllSessionsById.Emplace(FoundSession->SessionId, FoundSession);
							SearchResults.Add(FoundSession->SessionId);
						}

						Op->SetResult({ SearchResults });
					}
					else
					{
						Op->SetError(Errors::Unknown());						
					}

					PendingV1SessionSearchesPerUser.Remove(Op->GetParams().LocalAccountId);
				}
			});

			FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

			TArray<FUniqueNetIdRef> TargetUniqueNetIds = { ServicesOSSAdapter.GetAccountIdRegistry().GetIdValue(*OpParams.TargetUser).ToSharedRef() };

			// The cached search information is not used in this mode, but we'll still update it to not have more than one search at the same time
			PendingV1SessionSearchesPerUser.Emplace(OpParams.LocalAccountId, MakeShared<FOnlineSessionSearch>());

			SessionsInterface->FindFriendSession(*LocalUserId, TargetUniqueNetIds);
		}
		else
		{
			MakeMulticastAdapter(this, SessionsInterface->OnFindSessionsCompleteDelegates,
			[this, WeakOp = Op.AsWeak()](bool bWasSuccessful) mutable
			{
				if (TOnlineAsyncOpPtr<FFindSessions> Op = WeakOp.Pin())
				{
					if (bWasSuccessful)
					{
						TSharedRef<FOnlineSessionSearch>& PendingV1SessionSearch = PendingV1SessionSearchesPerUser.FindChecked(Op->GetParams().LocalAccountId);

						TArray<TSharedRef<FSessionCommon>> FoundSessions = BuildV2SessionSearchResults(PendingV1SessionSearch->SearchResults);
						TArray<FOnlineSessionIdHandle> SearchResults = SearchResultsUserMap.FindOrAdd(Op->GetParams().LocalAccountId);
						for (const TSharedRef<FSessionCommon>& FoundSession : FoundSessions)
						{
							AllSessionsById.Emplace(FoundSession->SessionId, FoundSession);
							SearchResults.Add(FoundSession->SessionId);
						}

						Op->SetResult({ SearchResults });
					}
					else
					{
						Op->SetError(Errors::Unknown());						
					}

					PendingV1SessionSearchesPerUser.Remove(Op->GetParams().LocalAccountId);
				}
			});

			TSharedRef<FOnlineSessionSearch>& PendingV1SessionSearch = PendingV1SessionSearchesPerUser.Emplace(OpParams.LocalAccountId, BuildV1SessionSearch(OpParams));

			SessionsInterface->FindSessions(*LocalUserId, PendingV1SessionSearch);
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FStartMatchmaking> FSessionsOSSAdapter::StartMatchmaking(FStartMatchmaking::Params&& Params)
{
	TOnlineAsyncOpRef<FStartMatchmaking> Op = GetOp<FStartMatchmaking>(MoveTemp(Params));
	const FStartMatchmaking::Params& OpParams = Op->GetParams();

	FOnlineError ParamsCheck = CheckStartMatchmakingParams(OpParams);
	if (ParamsCheck != Errors::Success())
	{
		Op->SetError(MoveTemp(ParamsCheck));
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FStartMatchmaking>& Op)
	{
		const FStartMatchmaking::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckStartMatchmakingState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		if (PendingV1SessionSearchesPerUser.Contains(OpParams.LocalAccountId))
		{
			Op.SetError(Errors::AlreadyPending());
			return;
		}

		FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

		TArray<FSessionMatchmakingUser> MatchMakingUsers;
		for (const TPair<FAccountId, FSessionMember>& LocalUser : OpParams.LocalAccounts)
		{
			FSessionMatchmakingUser NewMatchMakingUser { ServicesOSSAdapter.GetAccountIdRegistry().GetIdValue(LocalUser.Key).ToSharedRef() };

			for (const TPair<FSchemaAttributeId, FCustomSessionSetting>& Entry : LocalUser.Value.MemberSettings)
			{
				switch (Entry.Value.Data.VariantType)
				{
				case ESchemaAttributeType::Bool:
					NewMatchMakingUser.Attributes.Emplace(Entry.Key.ToString(), FVariantData(Entry.Value.Data.GetBoolean()));
					break;
				case ESchemaAttributeType::Double:
					NewMatchMakingUser.Attributes.Emplace(Entry.Key.ToString(), FVariantData(Entry.Value.Data.GetDouble()));
					break;
				case ESchemaAttributeType::Int64:
					NewMatchMakingUser.Attributes.Emplace(Entry.Key.ToString(), FVariantData(Entry.Value.Data.GetInt64()));
					break;
				case ESchemaAttributeType::String:
					NewMatchMakingUser.Attributes.Emplace(Entry.Key.ToString(), FVariantData(Entry.Value.Data.GetString()));
					break;
				}
			}

			MatchMakingUsers.Add(NewMatchMakingUser);
		}

		FOnlineSessionSettings NewSessionSettings = BuildV1Settings(OpParams.SessionSettings);

		/** We build a mock params struct for FindSessions and convert it to a V1 session search */
		FFindSessions::Params FindSessionsParams;
		FindSessionsParams.bFindLANSessions = false;
		FindSessionsParams.Filters = OpParams.SearchFilters;
		FindSessionsParams.LocalAccountId = OpParams.LocalAccountId;
		FindSessionsParams.MaxResults = 1;

		TSharedRef<FOnlineSessionSearch>& SessionSearch = PendingV1SessionSearchesPerUser.Emplace(OpParams.LocalAccountId, BuildV1SessionSearch(FindSessionsParams));

		// QUESTION: StartMatchmaking causes both OnMatchmakingComplete and OnStartMatchmakingComplete to trigger.
		// The first triggers the second so I used the latter, but the interface header did not specify if one should be used. Is this the correct one?
		SessionsInterface->StartMatchmaking(MatchMakingUsers, OpParams.SessionName, NewSessionSettings, SessionSearch, *MakeDelegateAdapter(this, [this, WeakOp = Op.AsWeak()](FName SessionName, const ::FOnlineError& ErrorDetails, const FSessionMatchmakingResults& Results) mutable
		{
			if (TOnlineAsyncOpPtr<FStartMatchmaking> Op = WeakOp.Pin())
			{
				if (!ErrorDetails.bSucceeded)
				{
					Op->SetError(Errors::Unknown()); // TODO: Proper error parsing
					return;
				}

				const FStartMatchmaking::Params& OpParams = Op->GetParams();

				TSharedRef<FSessionCommon> V2Session = BuildV2Session(SessionsInterface->GetNamedSession(SessionName));

				AllSessionsById.Emplace(V2Session->SessionId, V2Session);

				LocalSessionsByName.Emplace(OpParams.SessionName, V2Session->SessionId);

				NamedSessionUserMap.FindOrAdd(OpParams.LocalAccountId).AddUnique(OpParams.SessionName);

				Op->SetResult({ });

				FSessionJoined Event{ { Op->GetParams().LocalAccountId }, V2Session->SessionId };

				SessionEvents.OnSessionJoined.Broadcast(Event);

				// TODO: Pending multiple local user support
			}
		}));
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FJoinSession> FSessionsOSSAdapter::JoinSession(FJoinSession::Params&& Params)
{
	TOnlineAsyncOpRef<FJoinSession> Op = GetOp<FJoinSession>(MoveTemp(Params));
	const FJoinSession::Params& OpParams = Op->GetParams();

	FOnlineError ParamsCheck = CheckJoinSessionParams(OpParams);
	if (ParamsCheck != Errors::Success())
	{
		Op->SetError(MoveTemp(ParamsCheck));
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FJoinSession>& Op)
	{
		const FJoinSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckJoinSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		const FUniqueNetIdRef LocalUserId = Services.Get<FAuthOSSAdapter>()->GetUniqueNetId(OpParams.LocalAccountId).ToSharedRef();
		if (!LocalUserId->IsValid())
		{
			Op.SetError(Errors::InvalidUser());
			return;
		}

		MakeMulticastAdapter(this, SessionsInterface->OnJoinSessionCompleteDelegates,
		[this, WeakOp = Op.AsWeak()](FName SessionName, EOnJoinSessionCompleteResult::Type Result) mutable
		{
			if (TOnlineAsyncOpPtr<FJoinSession> Op = WeakOp.Pin())
			{
				if (Result != EOnJoinSessionCompleteResult::Success)
				{
					Op->SetError(Errors::Unknown());
					return;
				}

				const FJoinSession::Params& OpParams = Op->GetParams();

				TOnlineResult<FGetSessionById> GetSessionByIdResult = GetSessionById({ OpParams.LocalAccountId, OpParams.SessionId });
				if (GetSessionByIdResult.IsError())
				{
					// If no result is found, the id might be expired, which we should notify
					if (GetSessionIdRegistry().IsHandleExpired(OpParams.SessionId))
					{
						UE_LOG(LogTemp, Warning, TEXT("[FSessionsOSSAdapter::JoinSession] SessionId parameter [%s] is expired. Please call FindSessions to get an updated list of available sessions "), *ToLogString(OpParams.SessionId));
					}

					Op->SetError(MoveTemp(GetSessionByIdResult.GetErrorValue()));
					return;
				}

				TSharedRef<const ISession> FoundSession = GetSessionByIdResult.GetOkValue().Session;

				LocalSessionsByName.Emplace(OpParams.SessionName, OpParams.SessionId);

				NamedSessionUserMap.FindOrAdd(OpParams.LocalAccountId).AddUnique(OpParams.SessionName);

				if (FoundSession->GetSessionSettings().bPresenceEnabled)
				{
					FOnlineSessionIdHandle& PresenceSessionId = PresenceSessionsUserMap.FindOrAdd(OpParams.LocalAccountId);
					PresenceSessionId = FoundSession->GetSessionId();
				}

				// After successfully joining a session, we'll remove all related invites if any are found
				if (TMap<FOnlineSessionInviteIdHandle, TSharedRef<FSessionInvite>>* UserMap = SessionInvitesUserMap.Find(OpParams.LocalAccountId))
				{
					TArray<FOnlineSessionInviteIdHandle> InviteIdsToRemove;
					for (const TPair<FOnlineSessionInviteIdHandle, TSharedRef<FSessionInvite>>& Entry : *UserMap)
					{
						if (Entry.Value->SessionId == FoundSession->GetSessionId())
						{
							InviteIdsToRemove.Add(Entry.Key);
						}
					}
					for (const FOnlineSessionInviteIdHandle& InviteId : InviteIdsToRemove)
					{
						UserMap->Remove(InviteId);
					}
				}

				Op->SetResult({ });

				FSessionJoined Event{ { OpParams.LocalAccountId }, FoundSession->GetSessionId() };

				SessionEvents.OnSessionJoined.Broadcast(Event);
			}
		});

		TOnlineResult<FGetSessionById> GetSessionByIdResult = GetSessionById({ OpParams.LocalAccountId, OpParams.SessionId });
		if (GetSessionByIdResult.IsError())
		{
			Op.SetError(MoveTemp(GetSessionByIdResult.GetErrorValue()));
			return;
		}

		const TSharedRef<const ISession>& FoundSession = GetSessionByIdResult.GetOkValue().Session;

		FOnlineSessionSearchResult SearchResult;

		if (const FCustomSessionSetting* PingInMs = FoundSession->GetSessionSettings().CustomSettings.Find(OSS_ADAPTER_SESSIONS_PING_IN_MS))
		{
			SearchResult.PingInMs = (int32)PingInMs->Data.GetInt64();
		}

		SearchResult.Session = BuildV1Session(FoundSession);

		SessionsInterface->JoinSession(*LocalUserId, OpParams.SessionName, SearchResult);

		// TODO: Pending support for multiple local users
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FAddSessionMembers> FSessionsOSSAdapter::AddSessionMembers(FAddSessionMembers::Params&& Params)
{
	TOnlineAsyncOpRef<FAddSessionMembers> Op = GetOp<FAddSessionMembers>(MoveTemp(Params));
	const FAddSessionMembers::Params& OpParams = Op->GetParams();

	Op->Then([this](TOnlineAsyncOp<FAddSessionMembers>& Op)
	{
		const FAddSessionMembers::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckAddSessionMembersState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		SessionsInterface->AddOnRegisterPlayersCompleteDelegate_Handle(*MakeDelegateAdapter(this, [this, WeakOp = Op.AsWeak()](FName SessionName, const TArray<FUniqueNetIdRef>& Players, bool bWasSuccessful) mutable
		{
			// Clearing the delegate needs to be the last thing we do, as modifying captured variables after it would result in a crash

			if (TOnlineAsyncOpPtr<FAddSessionMembers> Op = WeakOp.Pin())
			{
				if (bWasSuccessful)
				{
					Op->SetResult({ });
				}
				else
				{
					Op->SetError(Errors::Unknown());
				}
			}

			SessionsInterface->ClearOnRegisterPlayersCompleteDelegates(this);
		}));

		FAuthOSSAdapter* Auth = Services.Get<FAuthOSSAdapter>();
		TArray<FUniqueNetIdRef> TargetUserNetIds;
		TArray<FAccountId> NewSessionMemberIds;
		OpParams.NewSessionMembers.GenerateKeyArray(NewSessionMemberIds);
		for (const FAccountId& TargetUser : NewSessionMemberIds)
		{
			FUniqueNetIdRef TargetUserNetId = Auth->GetUniqueNetId(TargetUser).ToSharedRef();
			if (TargetUserNetId->IsValid())
			{
				TargetUserNetIds.Add(TargetUserNetId);
			}
		}

		SessionsInterface->RegisterPlayers(OpParams.SessionName, TargetUserNetIds);
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FRemoveSessionMembers> FSessionsOSSAdapter::RemoveSessionMembers(FRemoveSessionMembers::Params&& Params)
{
	TOnlineAsyncOpRef<FRemoveSessionMembers> Op = GetOp<FRemoveSessionMembers>(MoveTemp(Params));
	const FRemoveSessionMembers::Params& OpParams = Op->GetParams();

	Op->Then([this](TOnlineAsyncOp<FRemoveSessionMembers>& Op)
	{
		const FRemoveSessionMembers::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckRemoveSessionMembersState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		SessionsInterface->AddOnUnregisterPlayersCompleteDelegate_Handle(*MakeDelegateAdapter(this, [this, WeakOp = Op.AsWeak()](FName SessionName, const TArray<FUniqueNetIdRef>& Players, bool bWasSuccessful) mutable
		{
			// Clearing the delegate needs to be the last thing we do, as modifying captured variables after it would result in a crash

			if (TOnlineAsyncOpPtr<FRemoveSessionMembers> Op = WeakOp.Pin())
			{
				if (bWasSuccessful)
				{
					Op->SetResult({ });
				}
				else
				{
					Op->SetError(Errors::Unknown());
				}
			}

			SessionsInterface->ClearOnUnregisterPlayersCompleteDelegates(this);
		}));

		FAuthOSSAdapter* Auth = Services.Get<FAuthOSSAdapter>();
		TArray<FUniqueNetIdRef> TargetUserNetIds;
		for (const FAccountId& TargetUser : OpParams.SessionMemberIds)
		{
			FUniqueNetIdRef TargetUserNetId = Auth->GetUniqueNetId(TargetUser).ToSharedRef();
			if (TargetUserNetId->IsValid())
			{
				TargetUserNetIds.Add(TargetUserNetId);
			}
		}

		SessionsInterface->UnregisterPlayers(OpParams.SessionName, TargetUserNetIds);
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FSendSessionInvite> FSessionsOSSAdapter::SendSessionInvite(FSendSessionInvite::Params&& Params)
{
	TOnlineAsyncOpRef<FSendSessionInvite> Op = GetOp<FSendSessionInvite>(MoveTemp(Params));
	const FSendSessionInvite::Params& OpParams = Op->GetParams();

	Op->Then([this](TOnlineAsyncOp<FSendSessionInvite>& Op)
	{
		const FSendSessionInvite::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckSendSessionInviteState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		FAuthOSSAdapter* Auth = Services.Get<FAuthOSSAdapter>();

		const FUniqueNetIdRef LocalUserId = Auth->GetUniqueNetId(OpParams.LocalAccountId).ToSharedRef();
		if (!LocalUserId->IsValid())
		{
			Op.SetError(Errors::InvalidUser());
			return;
		}

		TArray<FUniqueNetIdRef> TargetUserNetIds;
		for (const FAccountId& TargetUser : OpParams.TargetUsers)
		{
			FUniqueNetIdRef TargetUserNetId = Auth->GetUniqueNetId(TargetUser).ToSharedRef();
			if (TargetUserNetId->IsValid())
			{
				TargetUserNetIds.Add(TargetUserNetId);
			}
		}

		SessionsInterface->SendSessionInviteToFriends(*LocalUserId, OpParams.SessionName, TargetUserNetIds);

		Op.SetResult({ });
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FRegisterPlayers> FSessionsOSSAdapter::RegisterPlayers(FRegisterPlayers::Params&& Params)
{
	TOnlineAsyncOpRef<FRegisterPlayers> Op = GetOp<FRegisterPlayers>(MoveTemp(Params));
	Op->SetError(Errors::NotImplemented());
	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FUnregisterPlayers> FSessionsOSSAdapter::UnregisterPlayers(FUnregisterPlayers::Params&& Params)
{
	TOnlineAsyncOpRef<FUnregisterPlayers> Op = GetOp<FUnregisterPlayers>(MoveTemp(Params));
	Op->SetError(Errors::NotImplemented());
	return Op->GetHandle();
}

TOnlineResult<FGetResolvedConnectString> FSessionsOSSAdapter::GetResolvedConnectString(const FGetResolvedConnectString::Params& Params)
{
	if (Params.SessionId.IsValid())
	{
		TOnlineResult<FGetSessionById> Result = GetSessionById({ Params.LocalAccountId, Params.SessionId });
		if (Result.IsOk())
		{
			FOnlineSessionSearchResult SearchResult;
			SearchResult.Session = BuildV1Session(Result.GetOkValue().Session);

			FString ConnectString;

			if (SessionsInterface->GetResolvedConnectString(SearchResult, Params.PortType, ConnectString))
			{
				return TOnlineResult<FGetResolvedConnectString>({ ConnectString });
			}
			else
			{
				return TOnlineResult<FGetResolvedConnectString>(Errors::Unknown());
			}
		}
		else
		{
			return TOnlineResult<FGetResolvedConnectString>(Result.GetErrorValue());
		}
	}
	else
	{
		// No valid session id set
		return TOnlineResult<FGetResolvedConnectString>(Errors::InvalidParams());
	}
}

FOnlineSessionSettings FSessionsOSSAdapter::BuildV1Settings(const FSessionSettings& InSessionSettings) const
{
	FOnlineSessionSettings Result;

	Result.bAllowInvites = InSessionSettings.bAllowNewMembers;
	Result.bAllowJoinInProgress = InSessionSettings.bAllowNewMembers;
	Result.bAllowJoinViaPresence = InSessionSettings.bAllowNewMembers;
	Result.bAllowJoinViaPresenceFriendsOnly = InSessionSettings.bAllowNewMembers;
	Result.Settings.Add(OSS_ADAPTER_SESSIONS_ALLOW_SANCTIONED_PLAYERS, InSessionSettings.bAllowSanctionedPlayers);
	Result.Settings.Add(OSS_ADAPTER_SESSIONS_ALLOW_UNREGISTERED_PLAYERS, InSessionSettings.bAllowUnregisteredPlayers);
	Result.bAntiCheatProtected = InSessionSettings.bAntiCheatProtected;
	Result.bIsDedicated = InSessionSettings.bIsDedicatedServerSession;
	Result.bIsLANMatch = InSessionSettings.bIsLANSession;
	Result.bShouldAdvertise = InSessionSettings.bAllowNewMembers;
	if (const FCustomSessionSetting* BuildUniqueId = InSessionSettings.CustomSettings.Find(OSS_ADAPTER_SESSIONS_BUILD_UNIQUE_ID))
	{
		Result.BuildUniqueId = BuildUniqueId->Data.GetInt64();
	}
	if (const FCustomSessionSetting* BuildUniqueId = InSessionSettings.CustomSettings.Find(OSS_ADAPTER_SESSIONS_USE_LOBBIES_IF_AVAILABLE))
	{
		Result.bUseLobbiesIfAvailable = BuildUniqueId->Data.GetBoolean();
	}
	if (const FCustomSessionSetting* BuildUniqueId = InSessionSettings.CustomSettings.Find(OSS_ADAPTER_SESSIONS_USE_LOBBIES_VOICE_CHAT_IF_AVAILABLE))
	{
		Result.bUseLobbiesVoiceChatIfAvailable = BuildUniqueId->Data.GetBoolean();
	}
	Result.bUsesPresence = InSessionSettings.bPresenceEnabled;
	if (const FCustomSessionSetting* BuildUniqueId = InSessionSettings.CustomSettings.Find(OSS_ADAPTER_SESSIONS_USES_STATS))
	{
		Result.bUsesStats = BuildUniqueId->Data.GetBoolean();
	}
	Result.NumPrivateConnections = InSessionSettings.NumMaxPrivateConnections;
	Result.NumPublicConnections = InSessionSettings.NumMaxPublicConnections;
	Result.Settings.Add(OSS_ADAPTER_SESSIONS_SCHEMA_NAME, InSessionSettings.SchemaName.ToString());
	Result.SessionIdOverride = InSessionSettings.SessionIdOverride;

	Result.Settings = GetV1SessionSettings(InSessionSettings.CustomSettings);
	
	FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

	for (const TPair<FAccountId, FSessionMember>& Entry : InSessionSettings.SessionMembers)
	{
		Result.MemberSettings.Emplace(ServicesOSSAdapter.GetAccountIdRegistry().GetIdValue(Entry.Key).ToSharedRef(), ::FSessionSettings()); // TODO: Pending SchemaVariant work
	}

	return Result;
}

void FSessionsOSSAdapter::WriteV2SessionSettingsFromV1Session(const FOnlineSession* InSession, FSessionSettings& OutSettings) const
{
	InSession->SessionSettings.Settings.FindChecked(OSS_ADAPTER_SESSIONS_ALLOW_SANCTIONED_PLAYERS).Data.GetValue(OutSettings.bAllowSanctionedPlayers);
	InSession->SessionSettings.Settings.FindChecked(OSS_ADAPTER_SESSIONS_ALLOW_UNREGISTERED_PLAYERS).Data.GetValue(OutSettings.bAllowUnregisteredPlayers);
	OutSettings.bAntiCheatProtected = InSession->SessionSettings.bAntiCheatProtected;
	OutSettings.bPresenceEnabled = InSession->SessionSettings.bUsesPresence;
	OutSettings.bIsDedicatedServerSession = InSession->SessionSettings.bIsDedicated;
	OutSettings.bIsLANSession = InSession->SessionSettings.bIsLANMatch;
	OutSettings.NumMaxPrivateConnections = InSession->SessionSettings.NumPrivateConnections;
	OutSettings.NumMaxPublicConnections = InSession->SessionSettings.NumPublicConnections;
	OutSettings.NumOpenPrivateConnections = InSession->NumOpenPrivateConnections;
	OutSettings.NumOpenPublicConnections = InSession->NumOpenPublicConnections;
	FString SchemaNameStr;
	InSession->SessionSettings.Settings.FindChecked(OSS_ADAPTER_SESSIONS_SCHEMA_NAME).Data.GetValue(SchemaNameStr);
	OutSettings.SchemaName = FSchemaId(SchemaNameStr);
	OutSettings.SessionIdOverride = InSession->SessionSettings.SessionIdOverride;

	OutSettings.CustomSettings = GetV2SessionSettings(InSession->SessionSettings.Settings);

	FCustomSessionSetting BuildUniqueId;
	BuildUniqueId.Data.Set((int64)InSession->SessionSettings.BuildUniqueId);
	OutSettings.CustomSettings.Add(OSS_ADAPTER_SESSIONS_BUILD_UNIQUE_ID, BuildUniqueId);

	FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

	for (const TPair<FUniqueNetIdRef, ::FSessionSettings>& Entry : InSession->SessionSettings.MemberSettings)
	{
		FAccountId SessionMemberId = ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(Entry.Key);

		FSessionMember SessionMember;
		SessionMember.MemberSettings = GetV2SessionSettings(Entry.Value);

		OutSettings.SessionMembers.Emplace(SessionMemberId, SessionMember);
	}
}

void FSessionsOSSAdapter::WriteV2SessionSettingsFromV1NamedSession(const FNamedOnlineSession* InSession, FSessionSettings& OutSettings) const
{
	bool bPublicJoinable, bFriendJoinable, bInviteOnly, bAllowInvites;
	InSession->GetJoinability(bPublicJoinable, bFriendJoinable, bInviteOnly, bAllowInvites);

	OutSettings.bAllowNewMembers = bPublicJoinable || bFriendJoinable || bInviteOnly;
	OutSettings.JoinPolicy = bInviteOnly ? ESessionJoinPolicy::InviteOnly : bFriendJoinable ? ESessionJoinPolicy::FriendsOnly : ESessionJoinPolicy::Public; // We use Public as the default as closed sessions will have bAllowNewMembers as false

	FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

	for (const FUniqueNetIdRef& RegisteredPlayer : InSession->RegisteredPlayers)
	{
		OutSettings.RegisteredPlayers.Add(ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(RegisteredPlayer));
	}
}

FOnlineSession FSessionsOSSAdapter::BuildV1Session(const TSharedRef<const ISession> InSession) const
{
	return FOnlineSession(FSessionOSSAdapter::Cast(*InSession).GetV1Session());
}

TSharedRef<FSessionCommon> FSessionsOSSAdapter::BuildV2Session(const FOnlineSession* InSession) const
{
	TSharedRef<FSessionOSSAdapter> Session = MakeShared<FSessionOSSAdapter>(*InSession);

	FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

	Session->SessionId = GetSessionIdRegistry().FindOrAddHandle(InSession->SessionInfo->GetSessionId().AsShared());
	WriteV2SessionSettingsFromV1Session(InSession, Session->SessionSettings);

	if (const FNamedOnlineSession* NamedInSession = static_cast<const FNamedOnlineSession*>(InSession))
	{
		Session->OwnerAccountId = ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(NamedInSession->LocalOwnerId->AsShared());
		WriteV2SessionSettingsFromV1NamedSession(NamedInSession, Session->SessionSettings);
	}

	return Session;
}

TArray<TSharedRef<FSessionCommon>> FSessionsOSSAdapter::BuildV2SessionSearchResults(const TArray<FOnlineSessionSearchResult>& SessionSearchResults) const
{
	TArray<TSharedRef<FSessionCommon>> FoundSessions;

	for (const FOnlineSessionSearchResult& SearchResult : SessionSearchResults)
	{
		TSharedRef<FSessionCommon> FoundSession = BuildV2Session(&SearchResult.Session);

		FCustomSessionSetting PingInMs;
		PingInMs.Data.Set((int64)SearchResult.PingInMs);
		FoundSession->SessionSettings.CustomSettings.Emplace(OSS_ADAPTER_SESSIONS_PING_IN_MS, PingInMs);

		FoundSessions.Add(FoundSession);
	}

	return FoundSessions;
}

FOnlineSessionIdRegistryOSSAdapter& FSessionsOSSAdapter::GetSessionIdRegistry() const
{
	FOnlineSessionIdRegistryOSSAdapter* SessionIdRegistry = static_cast<FOnlineSessionIdRegistryOSSAdapter*>(FOnlineIdRegistryRegistry::Get().GetSessionIdRegistry(Services.GetServicesProvider()));
	check(SessionIdRegistry);
	return *SessionIdRegistry;
}

FOnlineSessionInviteIdRegistryOSSAdapter& FSessionsOSSAdapter::GetSessionInviteIdRegistry() const
{
	FOnlineSessionInviteIdRegistryOSSAdapter* SessionInviteIdRegistry = static_cast<FOnlineSessionInviteIdRegistryOSSAdapter*>(FOnlineIdRegistryRegistry::Get().GetSessionInviteIdRegistry(Services.GetServicesProvider()));
	check(SessionInviteIdRegistry);
	return *SessionInviteIdRegistry;
}

/* UE::Online */ }
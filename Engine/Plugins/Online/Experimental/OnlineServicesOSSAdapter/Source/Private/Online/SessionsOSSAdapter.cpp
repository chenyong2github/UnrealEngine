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

EOnlineComparisonOp::Type GetV1SessionSearchComparisonOp(const ESessionsComparisonOp& InValue)
{
	switch (InValue)
	{
	case ESessionsComparisonOp::Equals:				return EOnlineComparisonOp::Equals;
	case ESessionsComparisonOp::GreaterThan:		return EOnlineComparisonOp::GreaterThan;
	case ESessionsComparisonOp::GreaterThanEquals:	return EOnlineComparisonOp::GreaterThanEquals;
	case ESessionsComparisonOp::LessThan:			return EOnlineComparisonOp::LessThan;
	case ESessionsComparisonOp::LessThanEquals:		return EOnlineComparisonOp::LessThanEquals;
	case ESessionsComparisonOp::Near:				return EOnlineComparisonOp::Near;
	case ESessionsComparisonOp::NotEquals:			return EOnlineComparisonOp::NotEquals;
	}

	checkNoEntry();

	return EOnlineComparisonOp::Equals;
}

ESessionState GetV2SessionState(const EOnlineSessionState::Type InValue)
{
	switch (InValue)
	{
	case EOnlineSessionState::NoSession: return ESessionState::Invalid;
	case EOnlineSessionState::Creating: return ESessionState::Creating;
	case EOnlineSessionState::Pending: return ESessionState::Joining;
	case EOnlineSessionState::Starting: return ESessionState::Valid;
	case EOnlineSessionState::InProgress: return ESessionState::Valid;
	case EOnlineSessionState::Ending: return ESessionState::Valid;
	case EOnlineSessionState::Ended: return ESessionState::Valid;
	case EOnlineSessionState::Destroying: return ESessionState::Destroying;
	}

	checkNoEntry();
	return ESessionState::Invalid;
}

FCustomSessionSettingsMap GetV2SessionSettings(const ::FSessionSettings& InSessionSettings)
{
	FCustomSessionSettingsMap Result;

	// TODO: Pending SchemaVariant work

	return Result;
}

TSharedRef<FOnlineSessionSearch> BuildV1SessionSearch(const FFindSessions::Params& SearchParams)
{
	TSharedRef<FOnlineSessionSearch> Result = MakeShared<FOnlineSessionSearch>();

	Result->bIsLanQuery = SearchParams.bFindLANSessions;

	Result->MaxSearchResults = SearchParams.MaxResults;

	if (const FFindSessionsSearchFilter* PingBucketSize = Algo::FindBy(SearchParams.Filters, OSS_ADAPTER_SESSION_SEARCH_PING_BUCKET_SIZE, &FFindSessionsSearchFilter::Key))
	{
		Result->PingBucketSize = (int32)PingBucketSize->Value.Get<int64>();
	}

	if (const FFindSessionsSearchFilter* PlatformHash = Algo::FindBy(SearchParams.Filters, OSS_ADAPTER_SESSION_SEARCH_PLATFORM_HASH, &FFindSessionsSearchFilter::Key))
	{
		Result->PlatformHash = (int32)PlatformHash->Value.Get<int64>();
	}

	//for (const FFindSessionsSearchFilter& SearchFilter : SearchParams.Filters)
	//{
		// TODO: Pending SchemaVariant support
		// Result->QuerySettings.Set(SearchFilter.Key, SearchFilter.Value, GetV1SessionSearchComparisonOp(SearchFilter.ComparisonOp));
	//}

	return Result;
}

/** FSessionOSSAdapter */

FSessionOSSAdapter::FSessionOSSAdapter(const FOnlineSession& InSession)
	: V1Session(InSession)
{

}

const FSessionOSSAdapter& FSessionOSSAdapter::Cast(const FSession& InSession)
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

		FSessionInvite SessionInvite;
		SessionInvite.SenderId = ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(FromId.AsShared());
		SessionInvite.RecipientId = ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(UserId.AsShared());
		SessionInvite.InviteId = GetSessionInviteIdRegistry().BasicRegistry.FindOrAddHandle(AppId);
		SessionInvite.Session = BuildV2Session(&InviteResult.Session);

		FSessionInviteReceived Event{ ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(UserId.AsShared()), MakeShared<FSessionInvite>(MoveTemp(SessionInvite)) };

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
			TResult<TSharedRef<const FSession>, FOnlineError>(BuildV2Session(&InviteResult.Session)),
			EUISessionJoinRequestedSource::FromInvitation
		};
	}));

	// All delegates handling Settings or Participant changes in the V1 implementation will be handled with calls to UpdateSession, and the FSessionUpdated event
}

void FSessionsOSSAdapter::Shutdown()
{
	SessionsInterface->ClearOnSessionInviteReceivedDelegates(this);

	Super::Shutdown();
}

TOnlineResult<FGetAllSessions> FSessionsOSSAdapter::GetAllSessions(FGetAllSessions::Params&& Params) const
{
	TArray<TSharedRef<const FSession>> Sessions;

	for (const FName SessionName : RegisteredSessionNames)
	{
		if (FNamedOnlineSession* NamedSession = SessionsInterface->GetNamedSession(SessionName))
		{
			Sessions.Add(BuildV2Session(NamedSession));
		}
	}

	return TOnlineResult<FGetAllSessions>({ Sessions });
}

TOnlineResult<FGetSessionByName> FSessionsOSSAdapter::GetSessionByName(FGetSessionByName::Params&& Params) const
{
	if (FNamedOnlineSession* NamedSession = SessionsInterface->GetNamedSession(Params.LocalName))
	{
		return TOnlineResult<FGetSessionByName>({ BuildV2Session(NamedSession) });
	}
	else
	{
		return TOnlineResult<FGetSessionByName>(Errors::NotFound());
	}
}

TOnlineResult<FGetSessionById> FSessionsOSSAdapter::GetSessionById(FGetSessionById::Params&& Params) const
{
	FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

	for (const FName SessionName : RegisteredSessionNames)
	{
		if (FNamedOnlineSession* NamedSession = SessionsInterface->GetNamedSession(SessionName))
		{
			FOnlineSessionIdHandle SessionIdHandle = GetSessionIdRegistry().FindOrAddHandle(NamedSession->SessionInfo->GetSessionId().AsShared());

			if (SessionIdHandle == Params.IdHandle)
			{
				return TOnlineResult<FGetSessionById>({ BuildV2Session(NamedSession) });
			}
		}
	}

	return TOnlineResult<FGetSessionById>(Errors::NotFound());
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

		// Dedicated servers can update sessions so we won't check if LocalUserId is valid
		const FUniqueNetIdRef LocalUserId = Services.Get<FAuthOSSAdapter>()->GetUniqueNetId(OpParams.LocalUserId).ToSharedRef();

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

				RegisteredSessionNames.Add(SessionName);

				const FNamedOnlineSession* V1Session = SessionsInterface->GetNamedSession(SessionName);

				TSharedRef<FSession> V2Session = BuildV2Session(V1Session);

				Op->SetResult({ V2Session });
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

				if (TSharedRef<FSession>* FoundSession = LocalSessionsByName.Find(OpParams.SessionName))
				{
					Op->SetResult({ *FoundSession });

					FSessionUpdated Event { *FoundSession, OpParams.Mutations };

					SessionEvents.OnSessionUpdated.Broadcast(Event);
				}
				else
				{
					Op->SetError(Errors::NotFound());
				}
			}
		});

		const TSharedRef<FSession>& FoundSession = LocalSessionsByName.FindChecked(OpParams.SessionName);

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

				RegisteredSessionNames.Remove(SessionName);

				Op->SetResult({ });

				FSessionLeft SessionLeftEvent;
				SessionLeftEvent.LocalUserIds.Append(Op->GetParams().LocalUsers);

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

		const FUniqueNetIdRef LocalUserId = Services.Get<FAuthOSSAdapter>()->GetUniqueNetId(OpParams.LocalUserId).ToSharedRef();
		if (!LocalUserId->IsValid())
		{
			Op.SetError(Errors::InvalidUser());
			return;
		}

		if (PendingV1SessionSearchesPerUser.Contains(OpParams.LocalUserId))
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
							TSharedRef<FOnlineSessionSearch>& PendingV1SessionSearch = PendingV1SessionSearchesPerUser.FindChecked(Op->GetParams().LocalUserId);

							TArray<TSharedRef<const FSession>> FoundSessions = BuildV2SessionSearchResults(PendingV1SessionSearch->SearchResults);

							Op->SetResult({ MoveTemp(FoundSessions) });
						}
						else
						{
							Op->SetError(Errors::Unknown());
						}

						PendingV1SessionSearchesPerUser.Remove(Op->GetParams().LocalUserId);
					}
				});

				FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

				FUniqueNetIdRef TargetIdRef = FUniqueNetIdString::EmptyId();
				if (OpParams.TargetUser.IsSet())
				{
					TargetIdRef = ServicesOSSAdapter.GetAccountIdRegistry().GetIdValue(*OpParams.TargetUser).ToSharedRef();
				}

				PendingV1SessionSearchesPerUser.Emplace(OpParams.LocalUserId, MakeShared<FOnlineSessionSearch>());

				SessionsInterface->FindSessionById(*LocalUserId, *SessionIdRef, *TargetIdRef, *MakeDelegateAdapter(this, [this, WeakOp = Op.AsWeak()](int32 LocalUserNum, bool bWasSuccessful, const FOnlineSessionSearchResult& SingleSearchResult) mutable
				{
					if (TOnlineAsyncOpPtr<FFindSessions> Op = WeakOp.Pin())
					{
						if (!bWasSuccessful)
						{
							Op->SetError(Errors::Unknown());
							return;
						}

						TSharedRef<FOnlineSessionSearch>& PendingV1SessionSearch = PendingV1SessionSearchesPerUser.FindChecked(Op->GetParams().LocalUserId);

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
			int32 LocalUserNum = Services.Get<FAuthOSSAdapter>()->GetLocalUserNum(OpParams.LocalUserId);

			MakeMulticastAdapter(this, SessionsInterface->OnFindFriendSessionCompleteDelegates[LocalUserNum],
			[this, WeakOp = Op.AsWeak()](int32 LocalUserId, bool bWasSuccessful, const TArray<FOnlineSessionSearchResult>& FriendSearchResults)
			{
				if (TOnlineAsyncOpPtr<FFindSessions> Op = WeakOp.Pin())
				{
					if (bWasSuccessful)
					{
						TArray<TSharedRef<const FSession>> FoundSessions = BuildV2SessionSearchResults(FriendSearchResults);

						Op->SetResult({ MoveTemp(FoundSessions) });
					}
					else
					{
						Op->SetError(Errors::Unknown());						
					}

					PendingV1SessionSearchesPerUser.Remove(Op->GetParams().LocalUserId);
				}
			});

			FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

			TArray<FUniqueNetIdRef> TargetUniqueNetIds = { ServicesOSSAdapter.GetAccountIdRegistry().GetIdValue(*OpParams.TargetUser).ToSharedRef() };

			// The cached search information is not used in this mode, but we'll still update it to not have more than one search at the same time
			PendingV1SessionSearchesPerUser.Emplace(OpParams.LocalUserId, MakeShared<FOnlineSessionSearch>());

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
						TSharedRef<FOnlineSessionSearch>& PendingV1SessionSearch = PendingV1SessionSearchesPerUser.FindChecked(Op->GetParams().LocalUserId);

						TArray<TSharedRef<const FSession>> FoundSessions = BuildV2SessionSearchResults(PendingV1SessionSearch->SearchResults);

						Op->SetResult({ MoveTemp(FoundSessions) });
					}
					else
					{
						Op->SetError(Errors::Unknown());						
					}

					PendingV1SessionSearchesPerUser.Remove(Op->GetParams().LocalUserId);
				}
			});

			TSharedRef<FOnlineSessionSearch>& PendingV1SessionSearch = PendingV1SessionSearchesPerUser.Emplace(OpParams.LocalUserId, BuildV1SessionSearch(OpParams));

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

		if (PendingV1SessionSearchesPerUser.Contains(OpParams.LocalUserId))
		{
			Op.SetError(Errors::AlreadyPending());
			return;
		}

		FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

		TArray<FSessionMatchmakingUser> MatchMakingUsers;
		for (const TPair<FOnlineAccountIdHandle, FSessionMember>& LocalUser : OpParams.LocalUsers)
		{
			FSessionMatchmakingUser NewMatchMakingUser { ServicesOSSAdapter.GetAccountIdRegistry().GetIdValue(LocalUser.Key).ToSharedRef() };

			// TODO: Copy Attributes from session members parameters (Pending SchemaVariant)

			MatchMakingUsers.Add(NewMatchMakingUser);
		}

		FOnlineSessionSettings NewSessionSettings = BuildV1Settings(OpParams.SessionSettings);

		/** We build a mock params struct for FindSessions and convert it to a V1 session search */
		FFindSessions::Params FindSessionsParams;
		FindSessionsParams.bFindLANSessions = false;
		FindSessionsParams.Filters = OpParams.SearchFilters;
		FindSessionsParams.LocalUserId = OpParams.LocalUserId;
		FindSessionsParams.MaxResults = 1;

		TSharedRef<FOnlineSessionSearch>& SessionSearch = PendingV1SessionSearchesPerUser.Emplace(OpParams.LocalUserId, BuildV1SessionSearch(FindSessionsParams));

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

				TSharedRef<FSession> NewSession = BuildV2Session(SessionsInterface->GetNamedSession(SessionName));

				Op->SetResult({ NewSession });

				FSessionJoined Event{ { Op->GetParams().LocalUserId }, NewSession };

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

		const FUniqueNetIdRef LocalUserId = Services.Get<FAuthOSSAdapter>()->GetUniqueNetId(OpParams.LocalUserId).ToSharedRef();
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

				TOnlineResult<FGetSessionById> GetSessionByIdResult = GetSessionById({ OpParams.LocalUserId, OpParams.SessionId });
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

				RegisteredSessionNames.Add(SessionName);

				const TSharedRef<const FSession>& FoundSession = GetSessionByIdResult.GetOkValue().Session;

				Op->SetResult({ FoundSession });

				FSessionJoined Event{ { OpParams.LocalUserId }, FoundSession };

				SessionEvents.OnSessionJoined.Broadcast(Event);
			}
		});

		TOnlineResult<FGetSessionById> GetSessionByIdResult = GetSessionById({ OpParams.LocalUserId, OpParams.SessionId });
		if (GetSessionByIdResult.IsError())
		{
			Op.SetError(MoveTemp(GetSessionByIdResult.GetErrorValue()));
			return;
		}

		const TSharedRef<const FSession>& FoundSession = GetSessionByIdResult.GetOkValue().Session;

		FOnlineSessionSearchResult SearchResult;

		if (const FCustomSessionSetting* PingInMs = FoundSession->SessionSettings.CustomSettings.Find(OSS_ADAPTER_SESSIONS_PING_IN_MS))
		{
			SearchResult.PingInMs = (int32)PingInMs->Data.Get<int64>();
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
		TArray<FOnlineAccountIdHandle> NewSessionMemberIds;
		OpParams.NewSessionMembers.GenerateKeyArray(NewSessionMemberIds);
		for (const FOnlineAccountIdHandle& TargetUser : NewSessionMemberIds)
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
		for (const FOnlineAccountIdHandle& TargetUser : OpParams.SessionMemberIds)
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

		const FUniqueNetIdRef LocalUserId = Auth->GetUniqueNetId(OpParams.LocalUserId).ToSharedRef();
		if (!LocalUserId->IsValid())
		{
			Op.SetError(Errors::InvalidUser());
			return;
		}

		TArray<FUniqueNetIdRef> TargetUserNetIds;
		for (const FOnlineAccountIdHandle& TargetUser : OpParams.TargetUsers)
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
		TOnlineResult<FGetSessionById> Result = GetSessionById({ Params.LocalUserId, Params.SessionId });
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
		Result.BuildUniqueId = BuildUniqueId->Data.Get<int64>();
	}
	if (const FCustomSessionSetting* BuildUniqueId = InSessionSettings.CustomSettings.Find(OSS_ADAPTER_SESSIONS_USE_LOBBIES_IF_AVAILABLE))
	{
		Result.bUseLobbiesIfAvailable = BuildUniqueId->Data.Get<bool>();
	}
	if (const FCustomSessionSetting* BuildUniqueId = InSessionSettings.CustomSettings.Find(OSS_ADAPTER_SESSIONS_USE_LOBBIES_VOICE_CHAT_IF_AVAILABLE))
	{
		Result.bUseLobbiesVoiceChatIfAvailable = BuildUniqueId->Data.Get<bool>();
	}
	Result.bUsesPresence = InSessionSettings.bPresenceEnabled;
	if (const FCustomSessionSetting* BuildUniqueId = InSessionSettings.CustomSettings.Find(OSS_ADAPTER_SESSIONS_USES_STATS))
	{
		Result.bUsesStats = BuildUniqueId->Data.Get<bool>();
	}
	Result.NumPrivateConnections = InSessionSettings.NumMaxPrivateConnections;
	Result.NumPublicConnections = InSessionSettings.NumMaxPublicConnections;
	Result.Settings.Add(OSS_ADAPTER_SESSIONS_SCHEMA_NAME, InSessionSettings.SchemaName.ToString());
	Result.SessionIdOverride = InSessionSettings.SessionIdOverride;

	// TODO: Pending SchemaVariant work
	//Result.Settings =
	
	FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

	for (const TPair<FOnlineAccountIdHandle, FSessionMember>& Entry : InSessionSettings.SessionMembers)
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
	OutSettings.SchemaName = FName(SchemaNameStr);
	OutSettings.SessionIdOverride = InSession->SessionSettings.SessionIdOverride;

	// TODO: Pending SchemaVariant work
	//Result.CustomSettings = 

	FCustomSessionSetting BuildUniqueId;
	BuildUniqueId.Data.Set<int64>(InSession->SessionSettings.BuildUniqueId);
	OutSettings.CustomSettings.Add(OSS_ADAPTER_SESSIONS_BUILD_UNIQUE_ID, BuildUniqueId);

	FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

	for (TPair<FUniqueNetIdRef, ::FSessionSettings> Entry : InSession->SessionSettings.MemberSettings)
	{
		FOnlineAccountIdHandle SessionMemberId = ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(Entry.Key);
		FSessionMember SessionMember = FSessionMember(); // TODO: Pending SchemaVariant work

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

FOnlineSession FSessionsOSSAdapter::BuildV1Session(const TSharedRef<const FSession> InSession) const
{
	return FOnlineSession(FSessionOSSAdapter::Cast(*InSession).GetV1Session());
}

TSharedRef<FSession> FSessionsOSSAdapter::BuildV2Session(const FOnlineSession* InSession) const
{
	TSharedRef<FSessionOSSAdapter> Session = MakeShared<FSessionOSSAdapter>(*InSession);

	FOnlineServicesOSSAdapter& ServicesOSSAdapter = static_cast<FOnlineServicesOSSAdapter&>(Services);

	Session->SessionId = GetSessionIdRegistry().FindOrAddHandle(InSession->SessionInfo->GetSessionId().AsShared());
	WriteV2SessionSettingsFromV1Session(InSession, Session->SessionSettings);

	if (const FNamedOnlineSession* NamedInSession = static_cast<const FNamedOnlineSession*>(InSession))
	{
		Session->OwnerUserId = ServicesOSSAdapter.GetAccountIdRegistry().FindOrAddHandle(NamedInSession->LocalOwnerId->AsShared());
		Session->CurrentState = GetV2SessionState(NamedInSession->SessionState);
		WriteV2SessionSettingsFromV1NamedSession(NamedInSession, Session->SessionSettings);
	}

	return Session;
}

TArray<TSharedRef<const FSession>> FSessionsOSSAdapter::BuildV2SessionSearchResults(const TArray<FOnlineSessionSearchResult>& SessionSearchResults) const
{
	TArray<TSharedRef<const FSession>> FoundSessions;

	for (const FOnlineSessionSearchResult& SearchResult : SessionSearchResults)
	{
		TSharedRef<FSession> FoundSession = BuildV2Session(&SearchResult.Session);

		FCustomSessionSetting PingInMs;
		PingInMs.Data.Set<int64>((int64)SearchResult.PingInMs);
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
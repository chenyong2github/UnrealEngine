// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSessionEOSPlus.h"
#include "Misc/Guid.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "OnlineUserEOSPlus.h"
#include "EOSSettings.h"

FOnlineSessionEOSPlus::FOnlineSessionEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem)
	: EOSPlus(InSubsystem)
	, bUseEOSSessions(false)
{
	BaseSessionInterface = EOSPlus->BaseOSS->GetSessionInterface();
	EOSSessionInterface = EOSPlus->EosOSS->GetSessionInterface();
	check(BaseSessionInterface.IsValid() && EOSSessionInterface.IsValid());

	bUseEOSSessions = UEOSSettings::GetSettings().bUseEOSSessions;

	BaseSessionInterface->AddOnSessionUserInviteAcceptedDelegate_Handle(FOnSessionUserInviteAcceptedDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnSessionUserInviteAcceptedBase));
	BaseSessionInterface->AddOnSessionInviteReceivedDelegate_Handle(FOnSessionInviteReceivedDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnSessionInviteReceivedBase));
	BaseSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnCreateSessionComplete));

	IOnlineSessionPtr PrimaryInterface = BaseSessionInterface;
	if (bUseEOSSessions)
	{
		PrimaryInterface = EOSSessionInterface;

		EOSSessionInterface->AddOnSessionUserInviteAcceptedDelegate_Handle(FOnSessionUserInviteAcceptedDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnSessionUserInviteAcceptedEOS));
		EOSSessionInterface->AddOnSessionInviteReceivedDelegate_Handle(FOnSessionInviteReceivedDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnSessionInviteReceivedEOS));
	}

	// All of these depend upon which is our primary session interface
	PrimaryInterface->AddOnSessionFailureDelegate_Handle(FOnSessionFailureDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnSessionFailure));
	PrimaryInterface->AddOnStartSessionCompleteDelegate_Handle(FOnUpdateSessionCompleteDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnStartSessionComplete));
	PrimaryInterface->AddOnUpdateSessionCompleteDelegate_Handle(FOnUpdateSessionCompleteDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnUpdateSessionComplete));
	PrimaryInterface->AddOnEndSessionCompleteDelegate_Handle(FOnEndSessionCompleteDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnEndSessionComplete));
	PrimaryInterface->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnFindSessionsComplete));
	PrimaryInterface->AddOnCancelFindSessionsCompleteDelegate_Handle(FOnCancelFindSessionsCompleteDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnCancelFindSessionsComplete));
	PrimaryInterface->AddOnPingSearchResultsCompleteDelegate_Handle(FOnPingSearchResultsCompleteDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnPingSearchResultsComplete));
	PrimaryInterface->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnJoinSessionComplete));

	for (int32 LocalPlayerNum = 0; LocalPlayerNum < MAX_LOCAL_PLAYERS; LocalPlayerNum++)
	{
		PrimaryInterface->AddOnFindFriendSessionCompleteDelegate_Handle(LocalPlayerNum, FOnFindFriendSessionCompleteDelegate::CreateRaw(this, &FOnlineSessionEOSPlus::OnFindFriendSessionComplete));
	}
}

FOnlineSessionEOSPlus::~FOnlineSessionEOSPlus()
{
	BaseSessionInterface->ClearOnSessionUserInviteAcceptedDelegates(this);
	EOSSessionInterface->ClearOnSessionUserInviteAcceptedDelegates(this);
	BaseSessionInterface->ClearOnSessionInviteReceivedDelegates(this);
	EOSSessionInterface->ClearOnSessionInviteReceivedDelegates(this);
	BaseSessionInterface->ClearOnSessionFailureDelegates(this);
	EOSSessionInterface->ClearOnSessionFailureDelegates(this);

	BaseSessionInterface->ClearOnCreateSessionCompleteDelegates(this);
	BaseSessionInterface->ClearOnSessionFailureDelegates(this);
	BaseSessionInterface->ClearOnStartSessionCompleteDelegates(this);
	BaseSessionInterface->ClearOnUpdateSessionCompleteDelegates(this);
	BaseSessionInterface->ClearOnEndSessionCompleteDelegates(this);
	BaseSessionInterface->ClearOnFindSessionsCompleteDelegates(this);
	BaseSessionInterface->ClearOnCancelFindSessionsCompleteDelegates(this);
	BaseSessionInterface->ClearOnPingSearchResultsCompleteDelegates(this);
	BaseSessionInterface->ClearOnJoinSessionCompleteDelegates(this);

	EOSSessionInterface->ClearOnCreateSessionCompleteDelegates(this);
	EOSSessionInterface->ClearOnSessionFailureDelegates(this);
	EOSSessionInterface->ClearOnStartSessionCompleteDelegates(this);
	EOSSessionInterface->ClearOnUpdateSessionCompleteDelegates(this);
	EOSSessionInterface->ClearOnEndSessionCompleteDelegates(this);
	EOSSessionInterface->ClearOnFindSessionsCompleteDelegates(this);
	EOSSessionInterface->ClearOnCancelFindSessionsCompleteDelegates(this);
	EOSSessionInterface->ClearOnPingSearchResultsCompleteDelegates(this);
	EOSSessionInterface->ClearOnJoinSessionCompleteDelegates(this);

	for (int32 LocalPlayerNum = 0; LocalPlayerNum < MAX_LOCAL_PLAYERS; LocalPlayerNum++)
	{
		BaseSessionInterface->ClearOnFindFriendSessionCompleteDelegates(LocalPlayerNum, this);
		EOSSessionInterface->ClearOnFindFriendSessionCompleteDelegates(LocalPlayerNum, this);
	}
}

TSharedPtr<FUniqueNetIdEOSPlus> FOnlineSessionEOSPlus::GetNetIdPlus(const FString& SourceId)
{
	return EOSPlus->UserInterfacePtr->GetNetIdPlus(SourceId);
}

TSharedPtr<const FUniqueNetId> FOnlineSessionEOSPlus::GetBaseNetId(const FString& SourceId)
{
	return EOSPlus->UserInterfacePtr->GetBaseNetId(SourceId);
}

TSharedPtr<const FUniqueNetId> FOnlineSessionEOSPlus::GetEOSNetId(const FString& SourceId)
{
	return EOSPlus->UserInterfacePtr->GetEOSNetId(SourceId);
}

TArray<TSharedRef<const FUniqueNetId>> FOnlineSessionEOSPlus::GetBaseNetIds(const TArray<TSharedRef<const FUniqueNetId>>& Players)
{
	TArray<TSharedRef<const FUniqueNetId>> BaseIds;
	for (TSharedRef<const FUniqueNetId> SourceId : Players)
	{
		TSharedPtr<const FUniqueNetId> BaseId = GetBaseNetId(SourceId->ToString());
		if (BaseId.IsValid())
		{
			BaseIds.Add(BaseId.ToSharedRef());
		}
	}
	return BaseIds;
}

TArray<TSharedRef<const FUniqueNetId>> FOnlineSessionEOSPlus::GetEOSNetIds(const TArray<TSharedRef<const FUniqueNetId>>& Players)
{
	TArray<TSharedRef<const FUniqueNetId>> EOSIds;
	for (TSharedRef<const FUniqueNetId> SourceId : Players)
	{
		TSharedPtr<const FUniqueNetId> EOSId = GetEOSNetId(SourceId->ToString());
		if (EOSId.IsValid())
		{
			EOSIds.Add(EOSId.ToSharedRef());
		}
	}
	return EOSIds;
}

void FOnlineSessionEOSPlus::OnSessionUserInviteAcceptedBase(const bool bWasSuccessful, const int32 ControllerId, TSharedPtr<const FUniqueNetId> UserId, const FOnlineSessionSearchResult& InviteResult)
{
	if (!bWasSuccessful || !UserId.IsValid() || !InviteResult.IsValid())
	{
		return;
	}

	if (bUseEOSSessions)
	{
		FString SessionIdStr;
		InviteResult.Session.SessionSettings.Get(TEXT("EOSSessionId"), SessionIdStr);
		if (SessionIdStr.IsEmpty())
		{
			UE_LOG_ONLINE(Error, TEXT("Failed to get EOS session id from base session"));
			return;
		}
		TSharedPtr<const FUniqueNetId> SessionId = EOSSessionInterface->CreateSessionIdFromString(SessionIdStr);
		if (!SessionId.IsValid())
		{
			UE_LOG_ONLINE(Error, TEXT("Failed to get EOS session unique id from EOS session interface"));
			return;
		}
		TSharedPtr<const FUniqueNetId> EOSUserId = EOSPlus->EosOSS->GetIdentityInterface()->GetUniquePlayerId(ControllerId);
		if (!EOSUserId.IsValid())
		{
			UE_LOG_ONLINE(Error, TEXT("Failed to get EOS user id (%d)"), ControllerId);
			return;
		}
		// Do a search for the EOS session
		EOSSessionInterface->FindSessionById(*EOSUserId, *SessionId, FUniqueNetIdString(),
			FOnSingleSessionResultCompleteDelegate::CreateLambda([this, UserId](int32 LocalUserNum, bool bWasSuccessful, const FOnlineSessionSearchResult& EOSResult)
			{
				TSharedPtr<const FUniqueNetId> PlusUserId = EOSPlus->UserInterfacePtr->GetUniquePlayerId(LocalUserNum);
				// Now that we have the proper session trigger the invite
				TriggerOnSessionUserInviteAcceptedDelegates(bWasSuccessful, LocalUserNum, PlusUserId, EOSResult);
			}));
	}
	else
	{
		TriggerOnSessionUserInviteAcceptedDelegates(bWasSuccessful, ControllerId, GetNetIdPlus(UserId->ToString()), InviteResult);
	}
}

void FOnlineSessionEOSPlus::OnSessionUserInviteAcceptedEOS(const bool bWasSuccessful, const int32 ControllerId, TSharedPtr<const FUniqueNetId> UserId, const FOnlineSessionSearchResult& InviteResult)
{
	if (!bWasSuccessful || !UserId.IsValid() || !InviteResult.IsValid())
	{
		return;
	}

	// We can just forward this
	TriggerOnSessionUserInviteAcceptedDelegates(bWasSuccessful, ControllerId, GetNetIdPlus(UserId->ToString()), InviteResult);
}

void FOnlineSessionEOSPlus::OnSessionInviteReceivedBase(const FUniqueNetId& UserId, const FUniqueNetId& FromId, const FString& AppId, const FOnlineSessionSearchResult& InviteResult)
{
	TSharedPtr<FUniqueNetIdEOSPlus> Id = GetNetIdPlus(UserId.ToString());
	TSharedPtr<FUniqueNetIdEOSPlus> FriendId = GetNetIdPlus(FromId.ToString());
	if (!Id.IsValid() || !Id->GetEOSNetId().IsValid() || !FriendId.IsValid() || !InviteResult.IsValid())
	{
		return;
	}

	if (bUseEOSSessions)
	{
		FString SessionIdStr;
		InviteResult.Session.SessionSettings.Get(TEXT("EOSSessionId"), SessionIdStr);
		if (SessionIdStr.IsEmpty())
		{
			UE_LOG_ONLINE(Error, TEXT("Failed to get EOS session id from base session"));
			return;
		}
		TSharedPtr<const FUniqueNetId> SessionId = EOSSessionInterface->CreateSessionIdFromString(SessionIdStr);
		if (!SessionId.IsValid())
		{
			UE_LOG_ONLINE(Error, TEXT("Failed to get EOS session unique id from EOS session interface"));
			return;
		}
		// Do a search for the EOS session
		EOSSessionInterface->FindSessionById(*Id->GetEOSNetId(), *SessionId, FUniqueNetIdString(),
			FOnSingleSessionResultCompleteDelegate::CreateLambda([this, Id, FriendId, AppStr = FString(AppId)](int32 LocalUserNum, bool bWasSuccessful, const FOnlineSessionSearchResult& EOSResult)
			{
				// Now that we have the proper session trigger the invite
				TriggerOnSessionInviteReceivedDelegates(*Id, *FriendId, AppStr, EOSResult);
			}));
	}
	else
	{
		TriggerOnSessionInviteReceivedDelegates(*Id, *FriendId, AppId, InviteResult);
	}
}

void FOnlineSessionEOSPlus::OnSessionInviteReceivedEOS(const FUniqueNetId& UserId, const FUniqueNetId& FromId, const FString& AppId, const FOnlineSessionSearchResult& InviteResult)
{
	TSharedPtr<const FUniqueNetId> Id = GetNetIdPlus(UserId.ToString());
	TSharedPtr<const FUniqueNetId> FriendId = GetNetIdPlus(FromId.ToString());
	if (!Id.IsValid() || !FriendId.IsValid() || !InviteResult.IsValid())
	{
		return;
	}

	// We can just forward this
	TriggerOnSessionInviteReceivedDelegates(*Id, *FriendId, AppId, InviteResult);
}

void FOnlineSessionEOSPlus::OnSessionFailure(const FUniqueNetId& Player, ESessionFailure::Type Failure)
{
	TSharedPtr<FUniqueNetIdEOSPlus> PlusId = GetNetIdPlus(Player.ToString());
	if (!PlusId.IsValid())
	{
		return;
	}
	TriggerOnSessionFailureDelegates(*PlusId, Failure);
}

void FOnlineSessionEOSPlus::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	TriggerOnCreateSessionCompleteDelegates(SessionName, bWasSuccessful);
}

void FOnlineSessionEOSPlus::OnStartSessionComplete(FName SessionName, bool bWasSuccessful)
{
	TriggerOnStartSessionCompleteDelegates(SessionName, bWasSuccessful);
}

void FOnlineSessionEOSPlus::OnUpdateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	TriggerOnUpdateSessionCompleteDelegates(SessionName, bWasSuccessful);
}

void FOnlineSessionEOSPlus::OnEndSessionComplete(FName SessionName, bool bWasSuccessful)
{
	TriggerOnEndSessionCompleteDelegates(SessionName, bWasSuccessful);
}

void FOnlineSessionEOSPlus::OnFindSessionsComplete(bool bWasSuccessful)
{
	TriggerOnFindSessionsCompleteDelegates(bWasSuccessful);
}

void FOnlineSessionEOSPlus::OnCancelFindSessionsComplete(bool bWasSuccessful)
{
	TriggerOnCancelFindSessionsCompleteDelegates(bWasSuccessful);
}

void FOnlineSessionEOSPlus::OnPingSearchResultsComplete(bool bWasSuccessful)
{
	TriggerOnPingSearchResultsCompleteDelegates(bWasSuccessful);
}

void FOnlineSessionEOSPlus::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type JoinResult)
{
	TriggerOnJoinSessionCompleteDelegates(SessionName, JoinResult);
}

void FOnlineSessionEOSPlus::OnFindFriendSessionComplete(int32 LocalPlayerNum, bool bWasSuccessful, const TArray<FOnlineSessionSearchResult>& Results)
{
	TriggerOnFindFriendSessionCompleteDelegates(LocalPlayerNum, bWasSuccessful, Results);
}

FNamedOnlineSession* FOnlineSessionEOSPlus::AddNamedSession(FName SessionName, const FOnlineSessionSettings& SessionSettings)
{
	FScopeLock ScopeLock(&SessionLock);
	return new(Sessions) FNamedOnlineSession(SessionName, SessionSettings);
}

FNamedOnlineSession* FOnlineSessionEOSPlus::AddNamedSession(FName SessionName, const FOnlineSession& Session)
{
	FScopeLock ScopeLock(&SessionLock);
	return new(Sessions) FNamedOnlineSession(SessionName, Session);
}

bool FOnlineSessionEOSPlus::CreateSession(int32 HostingPlayerNum, FName SessionName, const FOnlineSessionSettings& NewSessionSettings)
{
	// If EOS is enabled, create there and mirror on platform and include EOS session info
	if (bUseEOSSessions)
	{
		OnCreateSessionCompleteDelegateHandleEOS = EOSSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
			FOnCreateSessionCompleteDelegate::CreateLambda([this, HostingPlayerNum](FName SessionName, bool bWasSuccessful)
		{
			if (bWasSuccessful)
			{
				// We need the session settings & session id from EOS
				FOnlineSessionSettings* Settings = EOSSessionInterface->GetSessionSettings(SessionName);
				if (Settings != nullptr)
				{
					// Mirror in the base interface
					BaseSessionInterface->CreateSession(HostingPlayerNum, SessionName, *Settings);

					EOSSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegateHandleEOS);

					return;
				}
				bWasSuccessful = false;
			}

			OnCreateSessionComplete(SessionName, bWasSuccessful);

			EOSSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegateHandleEOS);
		}));
		return EOSSessionInterface->CreateSession(HostingPlayerNum, SessionName, NewSessionSettings);
	}
	// Otherwise create the platform version
	return BaseSessionInterface->CreateSession(HostingPlayerNum, SessionName, NewSessionSettings);;
}

bool FOnlineSessionEOSPlus::CreateSession(const FUniqueNetId& HostingPlayerId, FName SessionName, const FOnlineSessionSettings& NewSessionSettings)
{
	FString HostingPlayerIdStr = HostingPlayerId.ToString();
	TSharedPtr<FUniqueNetIdEOSPlus> Id = GetNetIdPlus(HostingPlayerIdStr);
	if (!Id.IsValid() || !Id->GetEOSNetId().IsValid())
	{
		OnCreateSessionComplete(SessionName, false);
		return false;
	}

	// If EOS is enabled, create there and mirror on platform and include EOS session info
	if (bUseEOSSessions)
	{
		OnCreateSessionCompleteDelegateHandleEOS = EOSSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
			FOnCreateSessionCompleteDelegate::CreateLambda([this, HostingPlayerIdStr](FName SessionName, bool bWasSuccessful)
				{
					if (bWasSuccessful)
					{
						// We need the session settings & session id from EOS
						FOnlineSessionSettings* Settings = EOSSessionInterface->GetSessionSettings(SessionName);
						if (Settings != nullptr)
						{
							TSharedPtr<FUniqueNetIdEOSPlus> Id = GetNetIdPlus(HostingPlayerIdStr);

							// Mirror in the base interface
							BaseSessionInterface->CreateSession(*Id->GetBaseNetId(), SessionName, *Settings);

							EOSSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegateHandleEOS);

							return;
						}

						bWasSuccessful = false;
					}
					
					OnCreateSessionComplete(SessionName, bWasSuccessful);

					EOSSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegateHandleEOS);
				}));
		return EOSSessionInterface->CreateSession(*Id->GetEOSNetId(), SessionName, NewSessionSettings);
	}
	// Otherwise create the platform version
	return EOSSessionInterface->CreateSession(*Id->GetBaseNetId(), SessionName, NewSessionSettings);;
}

bool FOnlineSessionEOSPlus::StartSession(FName SessionName)
{
	if (bUseEOSSessions)
	{
		if (!EOSSessionInterface->StartSession(SessionName))
		{
			return false;
		}
	}
	return BaseSessionInterface->StartSession(SessionName);
}

bool FOnlineSessionEOSPlus::UpdateSession(FName SessionName, FOnlineSessionSettings& UpdatedSessionSettings, bool bShouldRefreshOnlineData)
{
	if (bUseEOSSessions)
	{
		if (!EOSSessionInterface->UpdateSession(SessionName, UpdatedSessionSettings, bShouldRefreshOnlineData))
		{
			return false;
		}
	}
	return BaseSessionInterface->UpdateSession(SessionName, UpdatedSessionSettings, bShouldRefreshOnlineData);
}

bool FOnlineSessionEOSPlus::EndSession(FName SessionName)
{
	if (bUseEOSSessions)
	{
		if (!EOSSessionInterface->EndSession(SessionName))
		{
			return false;
		}
	}
	return BaseSessionInterface->EndSession(SessionName);
}

FOnDestroySessionCompleteDelegate IgnoredDestroySessionDelegate;

bool FOnlineSessionEOSPlus::DestroySession(FName SessionName, const FOnDestroySessionCompleteDelegate& CompletionDelegate)
{
	if (bUseEOSSessions)
	{
		if (!EOSSessionInterface->DestroySession(SessionName, CompletionDelegate))
		{
			return false;
		}
	}
	return BaseSessionInterface->DestroySession(SessionName, IgnoredDestroySessionDelegate);
}

bool FOnlineSessionEOSPlus::IsPlayerInSession(FName SessionName, const FUniqueNetId& UniqueId)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(UniqueId.ToString());
		if (!Id.IsValid())
		{
			return false;
		}
		return EOSSessionInterface->IsPlayerInSession(SessionName, *Id);
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(UniqueId.ToString());
	if (!Id.IsValid())
	{
		return false;
	}
	return BaseSessionInterface->IsPlayerInSession(SessionName, *Id);
}

bool FOnlineSessionEOSPlus::StartMatchmaking(const TArray<TSharedRef<const FUniqueNetId>>& LocalPlayers, FName SessionName, const FOnlineSessionSettings& NewSessionSettings, TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	UE_LOG_ONLINE(Warning, TEXT("[FOnlineSessionEOSPlus::StartMatchmaking] Matchmaking is not supported in EOSPlus or EOS"));

	TriggerOnMatchmakingCompleteDelegates(SessionName, false);

	return true;
}

bool FOnlineSessionEOSPlus::CancelMatchmaking(int32 SearchingPlayerNum, FName SessionName)
{
	UE_LOG_ONLINE(Warning, TEXT("[FOnlineSessionEOSPlus::CancelMatchmaking] Matchmaking is not supported in EOSPlus or EOS"));

	TriggerOnCancelMatchmakingCompleteDelegates(SessionName, false);

	return true;
}

bool FOnlineSessionEOSPlus::CancelMatchmaking(const FUniqueNetId& SearchingPlayerId, FName SessionName)
{
	UE_LOG_ONLINE(Warning, TEXT("[FOnlineSessionEOSPlus::CancelMatchmaking] Matchmaking is not supported in EOSPlus or EOS"));

	TriggerOnCancelMatchmakingCompleteDelegates(SessionName, false);

	return true;
}

bool FOnlineSessionEOSPlus::FindSessions(int32 SearchingPlayerNum, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->FindSessions(SearchingPlayerNum, SearchSettings);
	}
	return BaseSessionInterface->FindSessions(SearchingPlayerNum, SearchSettings);
}

bool FOnlineSessionEOSPlus::FindSessions(const FUniqueNetId& SearchingPlayerId, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(SearchingPlayerId.ToString());
		if (!Id.IsValid())
		{
			return false;
		}
		return EOSSessionInterface->FindSessions(*Id, SearchSettings);
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(SearchingPlayerId.ToString());
	if (!Id.IsValid())
	{
		return false;
	}
	return BaseSessionInterface->FindSessions(*Id, SearchSettings);
}

bool FOnlineSessionEOSPlus::FindSessionById(const FUniqueNetId& SearchingUserId, const FUniqueNetId& SessionId, const FUniqueNetId& FriendId, const FOnSingleSessionResultCompleteDelegate& CompletionDelegate)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(SearchingUserId.ToString());
		if (!Id.IsValid())
		{
			return false;
		}
		TSharedPtr<const FUniqueNetId> Friend = GetEOSNetId(FriendId.ToString());
		if (!Friend.IsValid())
		{
			return false;
		}
		return EOSSessionInterface->FindSessionById(*Id, SessionId, *Friend, CompletionDelegate);
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(SearchingUserId.ToString());
	if (!Id.IsValid())
	{
		return false;
	}
	TSharedPtr<const FUniqueNetId> Friend = GetBaseNetId(FriendId.ToString());
	if (!Friend.IsValid())
	{
		return false;
	}
	return BaseSessionInterface->FindSessionById(*Id, SessionId, *Friend, CompletionDelegate);
}

bool FOnlineSessionEOSPlus::CancelFindSessions()
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->CancelFindSessions();
	}
	return BaseSessionInterface->CancelFindSessions();
}

bool FOnlineSessionEOSPlus::PingSearchResults(const FOnlineSessionSearchResult& SearchResult)
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->PingSearchResults(SearchResult);
	}
	return BaseSessionInterface->PingSearchResults(SearchResult);
}

bool FOnlineSessionEOSPlus::JoinSession(int32 PlayerNum, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->JoinSession(PlayerNum, SessionName, DesiredSession);
	}
	return BaseSessionInterface->JoinSession(PlayerNum, SessionName, DesiredSession);
}

bool FOnlineSessionEOSPlus::JoinSession(const FUniqueNetId& PlayerId, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(PlayerId.ToString());
		if (!Id.IsValid())
		{
			return false;
		}
		return EOSSessionInterface->JoinSession(*Id, SessionName, DesiredSession);
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(PlayerId.ToString());
	if (!Id.IsValid())
	{
		return false;
	}
	return BaseSessionInterface->JoinSession(*Id, SessionName, DesiredSession);
}

bool FOnlineSessionEOSPlus::FindFriendSession(int32 LocalUserNum, const FUniqueNetId& Friend)
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->FindFriendSession(LocalUserNum, Friend);
	}
	return BaseSessionInterface->FindFriendSession(LocalUserNum, Friend);
}

bool FOnlineSessionEOSPlus::FindFriendSession(const FUniqueNetId& LocalUserId, const FUniqueNetId& Friend)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(LocalUserId.ToString());
		if (!Id.IsValid())
		{
			return false;
		}
		TSharedPtr<const FUniqueNetId> FriendId = GetEOSNetId(Friend.ToString());
		if (!FriendId.IsValid())
		{
			return false;
		}
		return EOSSessionInterface->FindFriendSession(*Id, *FriendId);
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(LocalUserId.ToString());
	if (!Id.IsValid())
	{
		return false;
	}
	TSharedPtr<const FUniqueNetId> FriendId = GetBaseNetId(Friend.ToString());
	if (!FriendId.IsValid())
	{
		return false;
	}
	return BaseSessionInterface->FindFriendSession(*Id, *FriendId);
}

bool FOnlineSessionEOSPlus::FindFriendSession(const FUniqueNetId& LocalUserId, const TArray<TSharedRef<const FUniqueNetId>>& FriendList)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(LocalUserId.ToString());
		if (!Id.IsValid())
		{
			return false;
		}
		return EOSSessionInterface->FindFriendSession(*Id, GetEOSNetIds(FriendList));
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(LocalUserId.ToString());
	if (!Id.IsValid())
	{
		return false;
	}
	return BaseSessionInterface->FindFriendSession(*Id, GetBaseNetIds(FriendList));
}

bool FOnlineSessionEOSPlus::SendSessionInviteToFriend(int32 LocalUserNum, FName SessionName, const FUniqueNetId& Friend)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> FriendId = GetEOSNetId(Friend.ToString());
		if (!FriendId.IsValid())
		{
			return false;
		}
		return EOSSessionInterface->SendSessionInviteToFriend(LocalUserNum, SessionName, *FriendId);
	}
	TSharedPtr<const FUniqueNetId> FriendId = GetBaseNetId(Friend.ToString());
	if (!FriendId.IsValid())
	{
		return false;
	}
	return BaseSessionInterface->SendSessionInviteToFriend(LocalUserNum, SessionName, *FriendId);
}

bool FOnlineSessionEOSPlus::SendSessionInviteToFriend(const FUniqueNetId& LocalUserId, FName SessionName, const FUniqueNetId& Friend)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(LocalUserId.ToString());
		if (!Id.IsValid())
		{
			return false;
		}
		TSharedPtr<const FUniqueNetId> FriendId = GetEOSNetId(Friend.ToString());
		if (!FriendId.IsValid())
		{
			return false;
		}
		return EOSSessionInterface->SendSessionInviteToFriend(*Id, SessionName, *FriendId);
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(LocalUserId.ToString());
	if (!Id.IsValid())
	{
		return false;
	}
	TSharedPtr<const FUniqueNetId> FriendId = GetBaseNetId(Friend.ToString());
	if (!FriendId.IsValid())
	{
		return false;
	}
	return BaseSessionInterface->SendSessionInviteToFriend(*Id, SessionName, *FriendId);
}

bool FOnlineSessionEOSPlus::SendSessionInviteToFriends(int32 LocalUserNum, FName SessionName, const TArray<TSharedRef<const FUniqueNetId>>& Friends)
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->SendSessionInviteToFriends(LocalUserNum, SessionName, GetEOSNetIds(Friends));
	}
	return BaseSessionInterface->SendSessionInviteToFriends(LocalUserNum, SessionName, GetBaseNetIds(Friends));
}

bool FOnlineSessionEOSPlus::SendSessionInviteToFriends(const FUniqueNetId& LocalUserId, FName SessionName, const TArray<TSharedRef<const FUniqueNetId>>& Friends)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(LocalUserId.ToString());
		if (!Id.IsValid())
		{
			return false;
		}
		return EOSSessionInterface->SendSessionInviteToFriends(*Id, SessionName, GetEOSNetIds(Friends));
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(LocalUserId.ToString());
	if (!Id.IsValid())
	{
		return false;
	}
	return BaseSessionInterface->SendSessionInviteToFriends(*Id, SessionName, GetBaseNetIds(Friends));
}

bool FOnlineSessionEOSPlus::GetResolvedConnectString(FName SessionName, FString& ConnectInfo, FName PortType)
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->GetResolvedConnectString(SessionName, ConnectInfo, PortType);
	}
	return BaseSessionInterface->GetResolvedConnectString(SessionName, ConnectInfo, PortType);
}

bool FOnlineSessionEOSPlus::GetResolvedConnectString(const FOnlineSessionSearchResult& SearchResult, FName PortType, FString& ConnectInfo)
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->GetResolvedConnectString(SearchResult, PortType, ConnectInfo);
	}
	return BaseSessionInterface->GetResolvedConnectString(SearchResult, PortType, ConnectInfo);
}

FOnlineSessionSettings* FOnlineSessionEOSPlus::GetSessionSettings(FName SessionName)
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->GetSessionSettings(SessionName);
	}
	return BaseSessionInterface->GetSessionSettings(SessionName);
}

bool FOnlineSessionEOSPlus::RegisterPlayer(FName SessionName, const FUniqueNetId& PlayerId, bool bWasInvited)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(PlayerId.ToString());
		if (!Id.IsValid())
		{
			return false;
		}
		if (!EOSSessionInterface->RegisterPlayer(SessionName, *Id, bWasInvited))
		{
			return false;
		}
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(PlayerId.ToString());
	if (!Id.IsValid())
	{
		return false;
	}
	return BaseSessionInterface->RegisterPlayer(SessionName, *Id, bWasInvited);
}

bool FOnlineSessionEOSPlus::RegisterPlayers(FName SessionName, const TArray<TSharedRef<const FUniqueNetId>>& Players, bool bWasInvited)
{
	if (bUseEOSSessions)
	{
		if (!EOSSessionInterface->RegisterPlayers(SessionName, GetEOSNetIds(Players), bWasInvited))
		{
			return false;
		}
	}
	return BaseSessionInterface->RegisterPlayers(SessionName, GetBaseNetIds(Players), bWasInvited);
}

bool FOnlineSessionEOSPlus::UnregisterPlayer(FName SessionName, const FUniqueNetId& PlayerId)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(PlayerId.ToString());
		if (!Id.IsValid())
		{
			return false;
		}
		if (!EOSSessionInterface->UnregisterPlayer(SessionName, *Id))
		{
			return false;
		}
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(PlayerId.ToString());
	if (!Id.IsValid())
	{
		return false;
	}
	return BaseSessionInterface->UnregisterPlayer(SessionName, *Id);
}

bool FOnlineSessionEOSPlus::UnregisterPlayers(FName SessionName, const TArray<TSharedRef<const FUniqueNetId>>& Players)
{
	if (bUseEOSSessions)
	{
		if (!EOSSessionInterface->UnregisterPlayers(SessionName, GetEOSNetIds(Players)))
		{
			return false;
		}
	}
	return BaseSessionInterface->UnregisterPlayers(SessionName, GetBaseNetIds(Players));
}

FOnRegisterLocalPlayerCompleteDelegate IgnoredRegisterLocalPlayerDelegate;

void FOnlineSessionEOSPlus:: RegisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnRegisterLocalPlayerCompleteDelegate& Delegate)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(PlayerId.ToString());
		if (Id.IsValid())
		{
			EOSSessionInterface->RegisterLocalPlayer(*Id, SessionName, Delegate);
		}
		else
		{
			Delegate.ExecuteIfBound(PlayerId, EOnJoinSessionCompleteResult::UnknownError);
		}
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(PlayerId.ToString());
	if (!Id.IsValid())
	{
		return;
	}
	BaseSessionInterface->RegisterLocalPlayer(*Id, SessionName, IgnoredRegisterLocalPlayerDelegate);
}

FOnUnregisterLocalPlayerCompleteDelegate IgnoredUnregisterLocalPlayerDelegate;

void FOnlineSessionEOSPlus:: UnregisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnUnregisterLocalPlayerCompleteDelegate& Delegate)
{
	if (bUseEOSSessions)
	{
		TSharedPtr<const FUniqueNetId> Id = GetEOSNetId(PlayerId.ToString());
		if (Id.IsValid())
		{
			EOSSessionInterface->UnregisterLocalPlayer(*Id, SessionName, Delegate);
		}
		else
		{
			Delegate.ExecuteIfBound(PlayerId, false);
		}
	}
	TSharedPtr<const FUniqueNetId> Id = GetBaseNetId(PlayerId.ToString());
	if (!Id.IsValid())
	{
		return;
	}
	BaseSessionInterface->UnregisterLocalPlayer(*Id, SessionName, IgnoredUnregisterLocalPlayerDelegate);
}

int32 FOnlineSessionEOSPlus::GetNumSessions()
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->GetNumSessions();
	}
	return BaseSessionInterface->GetNumSessions();
}

void FOnlineSessionEOSPlus:: DumpSessionState()
{
	if (bUseEOSSessions)
	{
		EOSSessionInterface->GetNumSessions();
	}
	BaseSessionInterface->GetNumSessions();
}

FNamedOnlineSession* FOnlineSessionEOSPlus::GetNamedSession(FName SessionName)
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->GetNamedSession(SessionName);
	}
	return BaseSessionInterface->GetNamedSession(SessionName);
}

void FOnlineSessionEOSPlus:: RemoveNamedSession(FName SessionName)
{
	if (bUseEOSSessions)
	{
		EOSSessionInterface->RemoveNamedSession(SessionName);
		return;
	}
	BaseSessionInterface->RemoveNamedSession(SessionName);
}

EOnlineSessionState::Type FOnlineSessionEOSPlus::GetSessionState(FName SessionName) const
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->GetSessionState(SessionName);
	}
	return BaseSessionInterface->GetSessionState(SessionName);
}

bool FOnlineSessionEOSPlus::HasPresenceSession()
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->HasPresenceSession();
	}
	return BaseSessionInterface->HasPresenceSession();
}

TSharedPtr<const FUniqueNetId> FOnlineSessionEOSPlus::CreateSessionIdFromString(const FString& SessionIdStr)
{
	if (bUseEOSSessions)
	{
		return EOSSessionInterface->CreateSessionIdFromString(SessionIdStr);
	}
	return BaseSessionInterface->CreateSessionIdFromString(SessionIdStr);
}

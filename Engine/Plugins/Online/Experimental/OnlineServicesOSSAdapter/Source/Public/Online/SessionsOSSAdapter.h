// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/SessionsCommon.h"

#include "OnlineSubsystemTypes.h"
#include "OnlineSessionSettings.h"

#include "Online/OnlineIdOSSAdapter.h"

using IOnlineSessionPtr = TSharedPtr<class IOnlineSession>;
using IOnlineIdentityPtr = TSharedPtr<class IOnlineIdentity>;

namespace UE::Online {

class FOnlineServicesOSSAdapter;

/** Registry of V1 session ids in FUniqueNetIdRef format, indexed by FOnlineSessionIdHandle */
using FOnlineSessionIdRegistryOSSAdapter = TOnlineUniqueNetIdRegistry<OnlineIdHandleTags::FSession>;

using FOnlineSessionInviteIdRegistryOSSAdapter = FOnlineSessionInviteIdStringRegistry;

static FName OSS_ADAPTER_SESSIONS_ALLOW_SANCTIONED_PLAYERS = TEXT("OSS_ADAPTER_SESSIONS_ALLOW_SANCTIONED_PLAYERS");
static FName OSS_ADAPTER_SESSIONS_ALLOW_UNREGISTERED_PLAYERS = TEXT("OSS_ADAPTER_SESSIONS_ALLOW_UNREGISTERED_PLAYERS");
static FName OSS_ADAPTER_SESSIONS_USE_LOBBIES_IF_AVAILABLE = TEXT("OSS_ADAPTER_SESSIONS_USE_LOBBIES_IF_AVAILABLE");
static FName OSS_ADAPTER_SESSIONS_USE_LOBBIES_VOICE_CHAT_IF_AVAILABLE = TEXT("OSS_ADAPTER_SESSIONS_USE_LOBBIES_VOICE_CHAT_IF_AVAILABLE");
static FName OSS_ADAPTER_SESSIONS_USES_STATS = TEXT("OSS_ADAPTER_SESSIONS_USES_STATS");
static FName OSS_ADAPTER_SESSIONS_SCHEMA_NAME = TEXT("OSS_ADAPTER_SESSIONS_SCHEMA_NAME");
static FName OSS_ADAPTER_SESSIONS_BUILD_UNIQUE_ID = TEXT("OSS_ADAPTER_SESSIONS_BUILD_UNIQUE_ID");
static FName OSS_ADAPTER_SESSIONS_PING_IN_MS = TEXT("OSS_ADAPTER_SESSIONS_PING_IN_MS");

static FName OSS_ADAPTER_SESSION_SEARCH_PING_BUCKET_SIZE = TEXT("OSS_ADAPTER_SESSION_SEARCH_PING_BUCKET_SIZE");
static FName OSS_ADAPTER_SESSION_SEARCH_PLATFORM_HASH = TEXT("OSS_ADAPTER_SESSION_SEARCH_PLATFORM_HASH");

class FSessionOSSAdapter : public FSession
{
public:
	FSessionOSSAdapter() = default;
	FSessionOSSAdapter(const FSessionOSSAdapter& InSession) = default;
	FSessionOSSAdapter(const FOnlineSession& InSession);

	static const FSessionOSSAdapter& Cast(const FSession& InSession);

	const FOnlineSession& GetV1Session() const;

private:
	FOnlineSession V1Session;
};

class FSessionsOSSAdapter : public FSessionsCommon
{
public:
	using Super = FSessionsCommon;

	using FSessionsCommon::FSessionsCommon;

	FSessionsOSSAdapter(FOnlineServicesOSSAdapter& InOwningSubsystem);
	virtual ~FSessionsOSSAdapter() = default;

	// IOnlineComponent
	virtual void Initialize() override;
	virtual void Shutdown() override;

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
	virtual TOnlineAsyncOpHandle<FAddSessionMembers> AddSessionMembers(FAddSessionMembers::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FRemoveSessionMembers> RemoveSessionMembers(FRemoveSessionMembers::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FSendSessionInvite> SendSessionInvite(FSendSessionInvite::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FRegisterPlayers> RegisterPlayers(FRegisterPlayers::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUnregisterPlayers> UnregisterPlayers(FUnregisterPlayers::Params&& Params) override;

	TOnlineResult<FGetResolvedConnectString> GetResolvedConnectString(const FGetResolvedConnectString::Params& Params);

private:
	/** Builds a V1 Session Settings data type from a V2 equivalent */
	FOnlineSessionSettings BuildV1Settings(const FSessionSettings& InSessionSettings) const;
	/** Writes V1 Session Settings information into the passed V2 equivalent */
	void WriteV2SessionSettingsFromV1Session(const FOnlineSession* InSession, FSessionSettings& OutSettings) const;
	/** Writes V1 Session Settings information contained in Named session type into the passed V2 equivalent */
	void WriteV2SessionSettingsFromV1NamedSession(const FNamedOnlineSession* InSession, FSessionSettings& OutSettings) const;
	/** Builds a V1 Session data type from a V2 equivalent */
	FOnlineSession BuildV1Session(const TSharedRef<const FSession> InSession) const;
	/** Builds a V2 Session data type from a V1 equivalent */
	TSharedRef<FSession> BuildV2Session(const FOnlineSession* InSession) const;
	/** Builds a V2 Session Search Results array from the passed V1 equivalent types */
	TArray<TSharedRef<const FSession>> BuildV2SessionSearchResults(const TArray<FOnlineSessionSearchResult>& SessionSearchResults) const;

	FOnlineSessionIdRegistryOSSAdapter& GetSessionIdRegistry() const;
	FOnlineSessionInviteIdRegistryOSSAdapter& GetSessionInviteIdRegistry() const;

protected:
	IOnlineSessionPtr SessionsInterface;
	IOnlineIdentityPtr IdentityInterface;

	/** Record of all names for registered sessions. Needed for session retrieval methods */
	TSet<FName> RegisteredSessionNames;

	/** Cache of session search information to be used by ongoing session searches, indexed per user. Entries are removed upon search completion. */
	TMap<FOnlineAccountIdHandle, TSharedRef<FOnlineSessionSearch>> PendingV1SessionSearchesPerUser;
};

/* UE::Online */ }

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/Sessions.h"
#include "Online/OnlineComponent.h"
#include "Online/OnlineIdCommon.h"

namespace UE::Online {

class FOnlineServicesCommon;

static FName CONNECT_STRING_TAG = TEXT("CONNECT_STRING");

class FOnlineSessionIdStringRegistry : public IOnlineSessionIdRegistry
{
public:
	// Begin IOnlineSessionIdRegistry
	virtual inline FString ToLogString(const FOnlineSessionId& SessionId) const override
	{
		FString IdValue = BasicRegistry.FindIdValue(SessionId);

		if (IdValue.Len() == 0)
		{
			IdValue = FString(TEXT("[InvalidSessionID]"));
		}

		return IdValue;
	};

	virtual inline TArray<uint8> ToReplicationData(const FOnlineSessionId& SessionId) const override
	{
		const FString IdValue = BasicRegistry.FindIdValue(SessionId);
		const FTCHARToUTF8 IdValueUtf8(*IdValue);

		TArray<uint8> ReplicationData;
		ReplicationData.SetNumUninitialized(IdValueUtf8.Length());

		FMemory::Memcpy(ReplicationData.GetData(), IdValueUtf8.Get(), IdValueUtf8.Length());

		return ReplicationData;
	}

	virtual inline FOnlineSessionId FromReplicationData(const TArray<uint8>& ReplicationData) override
	{
		const FUTF8ToTCHAR IdValueTCHAR((char*)ReplicationData.GetData(), ReplicationData.Num());
		const FString IdValue = FString(IdValueTCHAR.Length(), IdValueTCHAR.Get());

		if (!IdValue.IsEmpty())
		{
			return BasicRegistry.FindOrAddHandle(IdValue);
		}

		return FOnlineSessionId();
	}
	// End IOnlineSessionIdRegistry

	inline bool IsSessionIdExpired(const FOnlineSessionId& InSessionId) const
	{
		return BasicRegistry.FindIdValue(InSessionId).IsEmpty();
	}

	FOnlineSessionIdStringRegistry(EOnlineServices OnlineServicesType)
		: BasicRegistry(OnlineServicesType)
	{

	}

	virtual ~FOnlineSessionIdStringRegistry() = default;

public:
	TOnlineBasicSessionIdRegistry<FString> BasicRegistry;
};

class FOnlineSessionInviteIdStringRegistry : public IOnlineSessionInviteIdRegistry
{
public:
	// Begin IOnlineSessionIdRegistry
	virtual inline FString ToLogString(const FSessionInviteId& SessionInviteId) const override
	{
		FString IdValue = BasicRegistry.FindIdValue(SessionInviteId);

		if (IdValue.Len() == 0)
		{
			IdValue = FString(TEXT("[InvalidSessionID]"));
		}

		return IdValue;
	};

	virtual inline TArray<uint8> ToReplicationData(const FSessionInviteId& SessionInviteId) const override
	{
		const FString IdValue = BasicRegistry.FindIdValue(SessionInviteId);
		const FTCHARToUTF8 IdValueUtf8(IdValue);

		TArray<uint8> ReplicationData;
		ReplicationData.SetNumUninitialized(IdValueUtf8.Length());

		FMemory::Memcpy(ReplicationData.GetData(), IdValueUtf8.Get(), IdValueUtf8.Length());

		return ReplicationData;
	}

	virtual inline FSessionInviteId FromReplicationData(const TArray<uint8>& ReplicationData) override
	{
		const FUTF8ToTCHAR IdValueTCHAR((char*)ReplicationData.GetData(), ReplicationData.Num());
		const FString IdValue = FString(IdValueTCHAR.Length(), IdValueTCHAR.Get());

		if (!IdValue.IsEmpty())
		{
			return BasicRegistry.FindOrAddHandle(IdValue);
		}

		return FSessionInviteId();
	}
	// End IOnlineSessionIdRegistry

	FOnlineSessionInviteIdStringRegistry(EOnlineServices OnlineServicesType)
		: BasicRegistry(OnlineServicesType)
	{

	}

	virtual ~FOnlineSessionInviteIdStringRegistry() = default;

public:
	TOnlineBasicSessionInviteIdRegistry<FString> BasicRegistry;
};

class ONLINESERVICESCOMMON_API FSessionCommon : public ISession
{
public:
	FSessionCommon() = default;
	FSessionCommon(const FSessionCommon& InSession) = default;
	virtual ~FSessionCommon() = default;

	// ISession
	virtual const FAccountId GetOwnerAccountId() const override				{ return OwnerAccountId; }
	virtual const FOnlineSessionId GetSessionId() const override			{ return GetSessionInfo().SessionId; }
	virtual const uint32 GetNumOpenConnections() const override				{ return SessionSettings.NumMaxConnections - SessionMembers.Num(); }
	virtual const FSessionInfo& GetSessionInfo() const override				{ return SessionInfo; }
	virtual const FSessionSettings GetSessionSettings() const override		{ return SessionSettings; }
	virtual const FSessionMemberIdsSet& GetSessionMembers() const override	{ return SessionMembers; }

	virtual bool IsJoinable() const override								{ return GetNumOpenConnections() > 0 && SessionSettings.bAllowNewMembers; }

	virtual FString ToLogString() const override;

	virtual void DumpState() const override;

protected:
	void DumpMemberData() const;
	void DumpSessionInfo() const;
	void DumpSessionSettings() const;

public:
	/** Session information that will remain constant throughout the session's lifetime */
	FSessionInfo SessionInfo;
	
	/** The following members can be updated, and such update will be transmitted via a FSessionUpdated event */

	/** The user who currently owns the session */
	FAccountId OwnerAccountId;

	/** Set of session properties that can be altered by the session owner */
	FSessionSettings SessionSettings;

	/* Set containing user ids for all the session members */
	FSessionMemberIdsSet SessionMembers;

	FSessionCommon& operator+=(const FSessionUpdate& SessionUpdate);
};

struct FGetMutableSessionByName
{
	static constexpr TCHAR Name[] = TEXT("GetMutableSessionByName");

	struct Params
	{
		FName LocalName;
	};

	struct Result
	{
		TSharedRef<FSessionCommon> Session;

		Result() = delete; // cannot default construct due to TSharedRef
	};
};

struct FGetMutableSessionById
{
	static constexpr TCHAR Name[] = TEXT("GetMutableSessionById");

	struct Params
	{
		FOnlineSessionId SessionId;
	};

	struct Result
	{
		TSharedRef<FSessionCommon> Session;

		Result() = delete; // cannot default construct due to TSharedRef
	};
};

struct FUpdateSessionSettingsImpl
{
	static constexpr TCHAR Name[] = TEXT("UpdateSessionSettingsImpl");

	struct Params
	{
		/** The local user agent which will perform the action. */
		FAccountId LocalAccountId;

		/** The local name for the session */
		FName SessionName;

		/** Changes to current session settings */
		FSessionSettingsUpdate Mutations;
	};

	struct Result
	{

	};
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
	virtual TOnlineResult<FGetAllSessions> GetAllSessions(FGetAllSessions::Params&& Params) const override;
	virtual TOnlineResult<FGetSessionByName> GetSessionByName(FGetSessionByName::Params&& Params) const override;
	virtual TOnlineResult<FGetSessionById> GetSessionById(FGetSessionById::Params&& Params) const override;
	virtual TOnlineResult<FGetPresenceSession> GetPresenceSession(FGetPresenceSession::Params&& Params) const override;
	virtual TOnlineResult<FIsPresenceSession> IsPresenceSession(FIsPresenceSession::Params&& Params) const override;
	virtual TOnlineResult<FSetPresenceSession> SetPresenceSession(FSetPresenceSession::Params&& Params) override;
	virtual TOnlineResult<FClearPresenceSession> ClearPresenceSession(FClearPresenceSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FCreateSession> CreateSession(FCreateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUpdateSessionSettings> UpdateSessionSettings(FUpdateSessionSettings::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FLeaveSession> LeaveSession(FLeaveSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FFindSessions> FindSessions(FFindSessions::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FStartMatchmaking> StartMatchmaking(FStartMatchmaking::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FJoinSession> JoinSession(FJoinSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAddSessionMember> AddSessionMember(FAddSessionMember::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FRemoveSessionMember> RemoveSessionMember(FRemoveSessionMember::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FSendSessionInvite> SendSessionInvite(FSendSessionInvite::Params&& Params) override;
	virtual TOnlineResult<FGetSessionInviteById> GetSessionInviteById(FGetSessionInviteById::Params&& Params) override;
	virtual TOnlineResult<FGetAllSessionInvites> GetAllSessionInvites(FGetAllSessionInvites::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FRejectSessionInvite> RejectSessionInvite(FRejectSessionInvite::Params&& Params) override;

	virtual TOnlineEvent<void(const FSessionJoined&)> OnSessionJoined() override;
	virtual TOnlineEvent<void(const FSessionLeft&)> OnSessionLeft() override;
	virtual TOnlineEvent<void(const FSessionUpdated&)> OnSessionUpdated() override;
	virtual TOnlineEvent<void(const FSessionInviteReceived&)> OnSessionInviteReceived() override;
	virtual TOnlineEvent<void(const FUISessionJoinRequested&)> OnUISessionJoinRequested() override;

protected:
	TOnlineResult<FGetMutableSessionByName> GetMutableSessionByName(FGetMutableSessionByName::Params&& Params) const;
	TOnlineResult<FGetMutableSessionById> GetMutableSessionById(FGetMutableSessionById::Params&& Params) const;

	void AddSessionInvite(const TSharedRef<FSessionInvite> SessionInvite, const TSharedRef<FSessionCommon> Session, const FAccountId& LocalAccountId);
	void AddSearchResult(const TSharedRef<FSessionCommon> Session, const FAccountId& LocalAccountId);
	void AddSessionWithReferences(const TSharedRef<FSessionCommon> Session, const FName& SessionName, const FAccountId& LocalAccountId, bool bIsPresenceSession);
	void AddSessionReferences(const FOnlineSessionId SessionId, const FName& SessionName, const FAccountId& LocalAccountId, bool bIsPresenceSession);

	void ClearSessionInvitesForSession(const FAccountId& LocalAccountId, const FOnlineSessionId SessionId);
	void ClearSessionReferences(const FOnlineSessionId SessionId, const FName& SessionName, const FAccountId& LocalAccountId);

	FSessionUpdate BuildSessionUpdate(const TSharedRef<FSessionCommon>& Session, const FSessionSettingsUpdate& UpdatedValues) const;

	virtual TFuture<TOnlineResult<FUpdateSessionSettingsImpl>> UpdateSessionSettingsImpl(FUpdateSessionSettingsImpl::Params&& Params);
	TOnlineResult<FAddSessionMember> AddSessionMemberImpl(const FAddSessionMember::Params& Params);
	TOnlineResult<FRemoveSessionMember> RemoveSessionMemberImpl(const FRemoveSessionMember::Params& Params);

	FOnlineError CheckCreateSessionParams(const FCreateSession::Params& Params);
	FOnlineError CheckCreateSessionState(const FCreateSession::Params& Params);

	FOnlineError CheckUpdateSessionSettingsParams(const FUpdateSessionSettings::Params& Params);
	FOnlineError CheckUpdateSessionSettingsState(const FUpdateSessionSettings::Params& Params);

	FOnlineError CheckFindSessionsParams(const FFindSessions::Params& Params);
	FOnlineError CheckFindSessionsState(const FFindSessions::Params& Params);

	FOnlineError CheckStartMatchmakingParams(const FStartMatchmaking::Params& Params);
	FOnlineError CheckStartMatchmakingState(const FStartMatchmaking::Params& Params);

	FOnlineError CheckJoinSessionParams(const FJoinSession::Params& Params);
	FOnlineError CheckJoinSessionState(const FJoinSession::Params& Params);

	FOnlineError CheckAddSessionMemberState(const FAddSessionMember::Params& Params);

	FOnlineError CheckRemoveSessionMemberState(const FRemoveSessionMember::Params& Params);

	FOnlineError CheckLeaveSessionState(const FLeaveSession::Params& Params);

	FOnlineError CheckSendSessionInviteState(const FSendSessionInvite::Params& Params);

	FOnlineError CheckRejectSessionInviteState(const FRejectSessionInvite::Params& Params);

private:
	TOptional<FOnlineError> CheckSessionExistsByName(const FAccountId& LocalAccountId, const FName& SessionName);

	void ClearSessionByName(const FName& SessionName);
	void ClearSessionById(const FOnlineSessionId& SessionId);

protected:

	struct FSessionEvents
	{
		TOnlineEventCallable<void(const FSessionJoined&)> OnSessionJoined;
		TOnlineEventCallable<void(const FSessionLeft&)> OnSessionLeft;
		TOnlineEventCallable<void(const FSessionUpdated&)> OnSessionUpdated;
		TOnlineEventCallable<void(const FSessionInviteReceived&)> OnSessionInviteReceived;
		TOnlineEventCallable<void(const FUISessionJoinRequested&)> OnUISessionJoinRequested;
	} SessionEvents;

	/** Map of named sessions a user is part of, indexed by user */
	TMap<FAccountId, TArray<FName>> NamedSessionUserMap;

	/** Map of sessions that local users are part of, indexed by their local name */
	TMap<FName, FOnlineSessionId> LocalSessionsByName;

	/** Map of sessions that local users have set as their presence session to appear in the platform UI. A user may not have set any session as their presence session. */
	TMap<FAccountId, FOnlineSessionId> PresenceSessionsUserMap;

	/** Cache for received session invites, mapped per user */
	TMap<FAccountId, TMap<FSessionInviteId, TSharedRef<FSessionInvite>>> SessionInvitesUserMap;

	/** Cache for the last set of session search results, mapped per user */
	TMap<FAccountId, TArray<FOnlineSessionId>> SearchResultsUserMap;

	/** Handle for an ongoing session search operation, mapped per user */
	TMap<FAccountId, TSharedRef<TOnlineAsyncOp<FFindSessions>>> CurrentSessionSearchHandlesUserMap;

	/** Set of every distinct FSession found, indexed by Id */
	TMap<FOnlineSessionId, TSharedRef<FSessionCommon>> AllSessionsById;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FGetMutableSessionByName::Params)
	ONLINE_STRUCT_FIELD(FGetMutableSessionByName::Params, LocalName)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetMutableSessionByName::Result)
	ONLINE_STRUCT_FIELD(FGetMutableSessionByName::Result, Session)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetMutableSessionById::Params)
	ONLINE_STRUCT_FIELD(FGetMutableSessionById::Params, SessionId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetMutableSessionById::Result)
	ONLINE_STRUCT_FIELD(FGetMutableSessionById::Result, Session)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUpdateSessionSettingsImpl::Params)
	ONLINE_STRUCT_FIELD(FUpdateSessionSettingsImpl::Params, LocalAccountId),
	ONLINE_STRUCT_FIELD(FUpdateSessionSettingsImpl::Params, SessionName),
	ONLINE_STRUCT_FIELD(FUpdateSessionSettingsImpl::Params, Mutations)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUpdateSessionSettingsImpl::Result)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }

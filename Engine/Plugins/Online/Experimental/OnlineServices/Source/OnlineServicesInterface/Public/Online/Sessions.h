// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOpHandle.h"
#include "Online/CoreOnline.h"
#include "Online/OnlineMeta.h"
#include "Misc/TVariant.h"

namespace UE::Online {

enum class ESessionsComparisonOp : uint8
{
	Equals,
	NotEquals,
	GreaterThan,
	GreaterThanEquals,
	LessThan,
	LessThanEquals,
	Near
};
ONLINESERVICESINTERFACE_API const TCHAR* LexToString(ESessionsComparisonOp EnumVal);
ONLINESERVICESINTERFACE_API void LexFromString(ESessionsComparisonOp& OutComparison, const TCHAR* InStr);

struct FFindSessionsSearchFilter
{
	// TODO: Will change after SchemaVariant work

	using FVariantType = TVariant<FString, int64, double, bool>;

	/** Name of the custom setting to be used as filter */
	FName Key;

	/** The type of comparison to perform */
	ESessionsComparisonOp ComparisonOp;

	/** Value to use when comparing the filter */
	FVariantType Value;
};

enum class ECustomSessionSettingVisibility : uint8
{
	/* Don't advertise via the online service or QoS data */
	DontAdvertise,
	/* Advertise via the server ping data only */
	ViaPingOnly,
	/* Advertise via the online service only */
	ViaOnlineService,
	/* Advertise via the online service and via the ping data */
	ViaOnlineServiceAndPing
};
ONLINESERVICESINTERFACE_API const TCHAR* LexToString(ECustomSessionSettingVisibility Value);
ONLINESERVICESINTERFACE_API void LexFromString(ECustomSessionSettingVisibility& Value, const TCHAR* InStr);

struct FCustomSessionSetting
{
	// TODO: Will share this type with Lobbies

	using FVariantType = TVariant<FString, int64, double, bool>;

	/** Setting value */
	FVariantType Data;

	/** How is this session setting advertised with the backend or searches */
	ECustomSessionSettingVisibility Visibility;

	/** Optional ID used in some platforms as the index instead of the setting name */
	int32 ID;
};

using FCustomSessionSettingsMap = TMap<FName, FCustomSessionSetting>;

struct FSessionMember
{
	FCustomSessionSettingsMap MemberSettings;
};

using FSessionMembersMap = TMap<FOnlineAccountIdHandle, FSessionMember>;

struct FSessionMemberUpdate
{
	FCustomSessionSettingsMap UpdatedMemberSettings;
	TArray<FName> RemovedMemberSettings;

	FSessionMemberUpdate& operator+=(FSessionMemberUpdate&& UpdatedValue);
};

using FSessionMemberUpdatesMap = TMap<FOnlineAccountIdHandle, FSessionMemberUpdate>;

enum class ESessionState : uint8
{
	Invalid,
	Creating,
	Joining,
	Valid,
	Leaving,
	Destroying
};
ONLINESERVICESINTERFACE_API const TCHAR* LexToString(ESessionState Value);
ONLINESERVICESINTERFACE_API void LexFromString(ESessionState& Value, const TCHAR* InStr);

enum class ESessionJoinPolicy : uint8
{
	Public,
	FriendsOnly,
	InviteOnly
};
ONLINESERVICESINTERFACE_API const TCHAR* LexToString(ESessionJoinPolicy Value);
ONLINESERVICESINTERFACE_API void LexFromString(ESessionJoinPolicy& Value, const TCHAR* InStr);

struct FRegisteredUser
{
	/* Whether a slot was reserved for this user upon registration */
	bool bHasReservedSlot = false;

	/* Whether the user is currently in the session */
	bool bIsInSession = false;
};

using FRegisteredUsersMap = TMap<FOnlineAccountIdHandle, FRegisteredUser>;

struct FSessionSettings
{
	/* The schema which will be applied to the session */
	FName SchemaName;

	/* Maximum number of public slots for session members */
	uint32 NumMaxPublicConnections;

	/* Number of available public slots for session members */
	uint32 NumOpenPublicConnections;

	/* Maximum number of private slots for session members */
	uint32 NumMaxPrivateConnections;

	/* Number of available private slots for session members */
	uint32 NumOpenPrivateConnections;

	/* Enum value describing the level of restriction to join the session */
	ESessionJoinPolicy JoinPolicy;

 	/* In platforms that support this feature, it will set the session id to this value. Might be subject to minimum and maximum length */
 	FString SessionIdOverride;

	/* Whether the session is only available in the local network and not via internet connection. Only available in some platforms */
	bool IsLANSession;

	/* Whether the session is configured to run as a dedicated server. Only available in some platforms */
	bool IsDedicatedServerSession;

	/* Whether new members are accepted in the session. Can vary depending on various factors, like the number of free slots available, or Join-In-Progress preferences when the session has started */
	bool bAllowNewMembers;

	/* Whether this session will allow sanctioned players to join it */
	bool bAllowSanctionedPlayers;

	/* Whether this session will allow unregistered players to join it */
	bool bAllowUnregisteredPlayers;

	/*Whether this is a secure session protected by anti-cheat services */
	bool bAntiCheatProtected;

	/* Whether this session will show its information in presence updates. Can only be set in one session at a time */
	bool bPresenceEnabled;
  
 	/* Map of user-defined settings to be passed to the platform APIs as additional information for various purposes */
 	FCustomSessionSettingsMap CustomSettings;
 
 	/* Map of session member ids to their corresponding user-defined settings */
 	FSessionMembersMap SessionMembers;
 
 	/* Map of registered users for this session. Can only be altered via RegisterPlayers and UnregisterPlayers */
 	FRegisteredUsersMap RegisteredUsers;
};

struct FSessionSettingsUpdate
{
	TOptional<FName> SchemaName;
	TOptional<uint32> NumMaxPublicConnections;
	TOptional<uint32> NumOpenPublicConnections;
	TOptional<uint32> NumMaxPrivateConnections;
	TOptional<uint32> NumOpenPrivateConnections;
	TOptional<ESessionJoinPolicy> JoinPolicy;
	TOptional<FString> SessionIdOverride;
	TOptional<bool> IsLANSession;
	TOptional<bool> IsDedicatedServerSession;
	TOptional<bool> bAllowNewMembers;
	TOptional<bool> bAllowSanctionedPlayers;
	TOptional<bool> bAllowUnregisteredPlayers;
	TOptional<bool> bAntiCheatProtected;
	TOptional<bool> bPresenceEnabled;

	FCustomSessionSettingsMap UpdatedCustomSettings;
	TArray<FName> RemovedCustomSettings;

	FSessionMemberUpdatesMap UpdatedSessionMembers;
	TArray<FOnlineAccountIdHandle> RemovedSessionMembers;

	FRegisteredUsersMap UpdatedRegisteredUsers;
	TArray<FOnlineAccountIdHandle> RemovedRegisteredUsers;

	FSessionSettingsUpdate& operator+=(FSessionSettingsUpdate&& UpdatedValue);
};

struct ONLINESERVICESINTERFACE_API FSession
{
	FSession();
	FSession(const FSession& InSession);

	/** The user who currently owns the session */
	FOnlineAccountIdHandle OwnerUserId;

	/** The id for the session, platform dependent */
	FOnlineSessionIdHandle SessionId;

	/** The current state of the session */
	ESessionState CurrentState = ESessionState::Invalid;

	/** Set of session properties that can be altered by the session owner */
	FSessionSettings SessionSettings;
};

struct FSessionInvite
{
	/* The user which the invite got sent to */
	FOnlineAccountIdHandle RecipientId;

	/* The user which sent the invite */
	FOnlineAccountIdHandle SenderId;

	/* The invite id, needed for retrieving session information and rejecting the invite */
	FString InviteId;

	/* Pointer to the session information */
	TSharedRef<const FSession> Session;

	// TODO: Default constructor will be deleted after we cache invites
};

struct FGetAllSessions
{
	static constexpr TCHAR Name[] = TEXT("GetAllSessions");

	struct Params
	{

	};

	struct Result
	{
		TArray<TSharedRef<const FSession>> Sessions;
	};
};

struct FGetSessionByName
{
	static constexpr TCHAR Name[] = TEXT("GetSessionByName");

	struct Params
	{
		FName LocalName;
	};

	struct Result
	{
		TSharedRef<const FSession> Session;

		Result() = delete; // cannot default construct due to TSharedRef
	};
};

struct FGetSessionById
{
	static constexpr TCHAR Name[] = TEXT("GetSessionById");

	struct Params
	{
		FOnlineSessionIdHandle IdHandle;
	};

	struct Result
	{
		TSharedRef<const FSession> Session;

		Result() = delete; // cannot default construct due to TSharedRef
	};
};

struct FCreateSession
{
	static constexpr TCHAR Name[] = TEXT("CreateSession");

	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** The local name for the session */
		FName SessionName;

		/** Settings object to define session properties during creation */
		FSessionSettings SessionSettings;

		/** Information for all local users who will join the session (includes the session creator) */
		FSessionMembersMap LocalUsers;
	};

	struct Result
	{
		TSharedRef<const FSession> Session;

		Result() = delete; // cannot default construct due to TSharedRef
	};
};

struct FUpdateSession
{
	static constexpr TCHAR Name[] = TEXT("UpdateSession");

	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** The local name for the session */
		FName SessionName;

		/** Changes to current session settings */
		FSessionSettingsUpdate Mutations;
	};

	struct Result
	{
		TSharedRef<const FSession> Session;

		Result() = delete; // cannot default construct due to TSharedRef
	};
};

struct FLeaveSession
{
	static constexpr TCHAR Name[] = TEXT("LeaveSession");

	struct Params
	{
		/* The local user agent which leaves the session*/
		FOnlineAccountIdHandle LocalUserId;

		/* The local name for the session. */
		FName SessionName;

		/** Ids for all local users who will leave the session (includes the main caller) */
		TArray<FOnlineAccountIdHandle> LocalUsers;

		/* Whether the call should attempt to destroy the session instead of just leave it */
		bool bDestroySession;
	};

	struct Result
	{

	};
};

struct FFindSessions
{
	static constexpr TCHAR Name[] = TEXT("FindSessions");

	struct Params
	{
		/* The local user agent which starts the session search*/
		FOnlineAccountIdHandle LocalUserId;

		/* Maximum number of results to return in one search */
		uint32 MaxResults;

		/* Filters to apply when searching for sessions. */
		TArray<FFindSessionsSearchFilter> Filters;

		/* Find sessions containing the target user. */
		TOptional<FOnlineAccountIdHandle> TargetUser;

		/* Find join info for the target session id. */
		TOptional<FOnlineSessionIdHandle> SessionId;
	};

	struct Result
	{
		TArray<TSharedRef<const FSession>> FoundSessions;
	};
};

struct FStartMatchmaking
{
	static constexpr TCHAR Name[] = TEXT("StartMatchmaking");

	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/* Information for all local users who will join the session (includes the session creator) */
		FSessionMembersMap LocalUsers;

		/* Local name for the session */
		FName SessionName;

		/* Preferred settings to be used during session creation */
		FSessionSettings SessionSettings;

		/* Filters to apply when searching for sessions */
		TArray<FFindSessionsSearchFilter> SearchFilters;
	};

	struct Result
	{
		TSharedRef<const FSession> Session;

		Result() = delete; // cannot default construct due to TSharedRef
	};
};

struct FJoinSession
{
	static constexpr TCHAR Name[] = TEXT("JoinSession");

	struct Params
	{
		/* The local user agent which starts the join operation*/
		FOnlineAccountIdHandle LocalUserId;

		/* Local name for the session */
		FName SessionName;

		/* A reference to the session to be joined. */
		TSharedRef<const FSession> Session; // TODO: FOnlineSessionIdHandle to be used after we cache search results

		/* Information for all local users who will join the session (includes the session creator)*/
		FSessionMembersMap LocalUsers;
	};

	struct Result
	{
		TSharedRef<const FSession> Session;

		Result() = delete; // cannot default construct due to TSharedRef
	};
};

struct FSendSessionInvite
{
	static constexpr TCHAR Name[] = TEXT("SendSessionInvite");

	struct Params
	{
		/* The local user agent which sends the invite*/
		FOnlineAccountIdHandle LocalUserId;

		/* The local name for the session. */
		FName SessionName;

		/* Array of id handles for users to which the invites will be sent */
		TArray<FOnlineAccountIdHandle> TargetUsers;
	};

	struct Result
	{

	};
};

struct FRejectSessionInvite
{
	static constexpr TCHAR Name[] = TEXT("RejectSessionInvite");

	struct Params
	{
		/* The local user agent which started the query*/
		FOnlineAccountIdHandle LocalUserId;

		/* The invite to be rejected */
		TSharedRef<const FSessionInvite> SessionInvite; // TODO: FOnlineSessionInviteIdHandle to be used after we cache invites
	};

	struct Result
	{
	};
};

struct FRegisterPlayers
{
	static constexpr TCHAR Name[] = TEXT("RegisterPlayers");

	struct Params
	{
		/* The local name for the session. */
		FName SessionName;

		/* Array of users which will be registered */
		TArray<FOnlineAccountIdHandle> TargetUsers;

		/* Whether a slot should be saved for the registered users */
		bool bReserveSlot;
	};

	struct Result
	{
	};
};

struct FUnregisterPlayers
{
	static constexpr TCHAR Name[] = TEXT("UnregisterPlayers");

	struct Params
	{
		/* The local name for the session. */
		FName SessionName;

		/* Array of users which will be unregistered */
		TArray<FOnlineAccountIdHandle> TargetUsers;

		/* Whether unregistered players should be removed from the session, if they are in it */
		bool bRemoveUnregisteredPlayers;
	};

	struct Result
	{
	};
};

/* Events */

struct FSessionJoined
{
	/* The local users which joined the session */
	TArray<FOnlineAccountIdHandle> LocalUserIds;

	/* A shared reference to the session joined. */
	TSharedRef<const FSession> Session;

	FSessionJoined() = delete; // cannot default construct due to TSharedRef
};

struct FSessionLeft
{
	/* The local users which left the session */
	TArray<FOnlineAccountIdHandle> LocalUserIds;
};

struct FSessionUpdated
{
	/* Reference to the updated session object */
	TSharedRef<const FSession> UpdatedSession;

	/* Updated session settings */
	FSessionSettingsUpdate UpdatedSettings;

	FSessionUpdated() = delete; // cannot default construct due to TSharedRef
};

struct FSessionInviteReceived
{
	/* The local user which received the invite */
	FOnlineAccountIdHandle LocalUserId;

	/** The session invite the local user was sent, or the online error if there was a failure retrieving the session for it*/
	TSharedRef<const FSessionInvite> SessionInvite;

	FSessionInviteReceived() = delete; // cannot default construct due to TSharedRef
};

/** Session join requested source */
enum class EUISessionJoinRequestedSource : uint8
{
	/** Unspecified by the online service */
	Unspecified,
	/** From an invitation */
	FromInvitation,
};
ONLINESERVICESINTERFACE_API const TCHAR* LexToString(EUISessionJoinRequestedSource UISessionJoinRequestedSource);
ONLINESERVICESINTERFACE_API void LexFromString(EUISessionJoinRequestedSource& OutUISessionJoinRequestedSource, const TCHAR* InStr);

struct FUISessionJoinRequested
{
	/** The local user associated with the join request. */
	FOnlineAccountIdHandle LocalUserId;

	/** The session the local user requested to join, or the online error if there was a failure retrieving it */
	TResult<TSharedRef<const FSession>, FOnlineError> Result;

	/** Join request source */
	EUISessionJoinRequestedSource JoinRequestedSource = EUISessionJoinRequestedSource::Unspecified;

	FUISessionJoinRequested() = delete; // cannot default construct due to TSharedRef
};

class ISessions
{
public:
	/**
	 * Get an array of all session objects.
	 * 
	 * @params Parameters for the GetAllSessions call
	 * return
	 */
	virtual TOnlineResult<FGetAllSessions> GetAllSessions(FGetAllSessions::Params&& Params) const = 0;

	/**
	 * Get the session object with a given local name.
	 *
	 * @params Parameters for the GetSessionByName call
	 * return
	 */
	virtual TOnlineResult<FGetSessionByName> GetSessionByName(FGetSessionByName::Params&& Params) const = 0;

	/**
	 * Get the session object with a given id handle.
	 *
	 * @params Parameters for the GetSessionById call
	 * return
	 */
	virtual TOnlineResult<FGetSessionById> GetSessionById(FGetSessionById::Params&& Params) const = 0;

	/**
	 * Create and join a new session.
	 *
	 * @param Parameters for the CreateSession call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FCreateSession> CreateSession(FCreateSession::Params&& Params) = 0;

	/**
	 * Update a given session's settings.
	 *
	 * @param Parameters for the UpdateSession call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FUpdateSession> UpdateSession(FUpdateSession::Params&& Params) = 0;

	/**
	 * Leave and optionally destroy a given session.
	 *
	 * @param Parameters for the LeaveSession call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FLeaveSession> LeaveSession(FLeaveSession::Params&& Params) = 0;

	/**
	 * Queries the API session service for sessions matching the given parameters.
	 *
	 * @param Parameters for the FindSessions call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FFindSessions> FindSessions(FFindSessions::Params&& Params) = 0;

	/**
	 * Starts the matchmaking process, which will either create a session with the passed parameters, or join one that matches the passed search filters.
	 *
	 * @param Parameters for the StartMatchmaking call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FStartMatchmaking> StartMatchmaking(FStartMatchmaking::Params&& Params) = 0;

	/**
	 * Starts the join process for the given session for all users provided.
	 *
	 * @param Parameters for the JoinSession call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FJoinSession> JoinSession(FJoinSession::Params&& Params) = 0;

	/**
	 * Sends an invite to the named session to all given users.
	 *
	 * @param Parameters for the SendSessionInvite call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FSendSessionInvite> SendSessionInvite(FSendSessionInvite::Params&& Params) = 0;

	/**
	 * Rejects a given session invite for a user.
	 *
	 * @param Parameters for the RejectSessionInvite call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FRejectSessionInvite> RejectSessionInvite(FRejectSessionInvite::Params&& Params) = 0;

	/**
	 * Registers given players in the named session.
	 *
	 * @param Parameters for the RegisterPlayers call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FRegisterPlayers> RegisterPlayers(FRegisterPlayers::Params&& Params) = 0;

	/**
	 * Unregisters given players from the named session.
	 *
	 * @param Parameters for the UnregisterPlayers call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FUnregisterPlayers> UnregisterPlayers(FUnregisterPlayers::Params&& Params) = 0;

	/* Events */

	/**
	 * Get the event that is triggered when a session is joined.
	 * This event will trigger as a result of creating or joining a session.
	 *
	 * @return
	 */
	virtual TOnlineEvent<void(const FSessionJoined&)> OnSessionJoined() = 0;

	/**
	 * Get the event that is triggered when a session is left.
	 * This event will trigger as a result of leaving or destroying a session.
	 *
	 * @return
	 */
	virtual TOnlineEvent<void(const FSessionLeft&)> OnSessionLeft() = 0;

	/**
	 * Get the event that is triggered when a session invite is accepted.
	 * This event will trigger as a result of accepting a platform session invite.
	 *
	 * @return
	 */
	virtual TOnlineEvent<void(const FSessionUpdated&)> OnSessionUpdated() = 0;

	/**
	 * Get the event that is triggered when a session invite is received.
	 * This event will trigger as a result of receiving a platform session invite.
	 *
	 * @return
	 */
	virtual TOnlineEvent<void(const FSessionInviteReceived&)> OnSessionInviteReceived() = 0;

	/**
	 * Get the event that is triggered when a session is joined via UI.
	 * This event will trigger as a result of joining a session via the platform UI.
	 *
	 * @return
	 */
	virtual TOnlineEvent<void(const FUISessionJoinRequested&)> OnUISessionJoinRequested() = 0;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FFindSessionsSearchFilter)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FCustomSessionSetting)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FSessionMember)
	ONLINE_STRUCT_FIELD(FSessionMember, MemberSettings)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FSessionMemberUpdate)
	ONLINE_STRUCT_FIELD(FSessionMemberUpdate, UpdatedMemberSettings),
	ONLINE_STRUCT_FIELD(FSessionMemberUpdate, RemovedMemberSettings)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FRegisteredUser)
	ONLINE_STRUCT_FIELD(FRegisteredUser, bHasReservedSlot),
	ONLINE_STRUCT_FIELD(FRegisteredUser, bIsInSession)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FSessionSettings)
	ONLINE_STRUCT_FIELD(FSessionSettings, SchemaName),
	ONLINE_STRUCT_FIELD(FSessionSettings, NumMaxPublicConnections),
	ONLINE_STRUCT_FIELD(FSessionSettings, NumOpenPublicConnections),
	ONLINE_STRUCT_FIELD(FSessionSettings, NumMaxPrivateConnections),
	ONLINE_STRUCT_FIELD(FSessionSettings, NumOpenPrivateConnections),
	ONLINE_STRUCT_FIELD(FSessionSettings, JoinPolicy),
	ONLINE_STRUCT_FIELD(FSessionSettings, SessionIdOverride),
	ONLINE_STRUCT_FIELD(FSessionSettings, IsLANSession),
	ONLINE_STRUCT_FIELD(FSessionSettings, IsDedicatedServerSession),
	ONLINE_STRUCT_FIELD(FSessionSettings, bAllowNewMembers),
	ONLINE_STRUCT_FIELD(FSessionSettings, bAllowSanctionedPlayers),
	ONLINE_STRUCT_FIELD(FSessionSettings, bAllowUnregisteredPlayers),
	ONLINE_STRUCT_FIELD(FSessionSettings, bAntiCheatProtected),
	ONLINE_STRUCT_FIELD(FSessionSettings, bPresenceEnabled),
	ONLINE_STRUCT_FIELD(FSessionSettings, CustomSettings),
	ONLINE_STRUCT_FIELD(FSessionSettings, SessionMembers),
	ONLINE_STRUCT_FIELD(FSessionSettings, RegisteredUsers)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FSessionSettingsUpdate)
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, SchemaName),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, NumMaxPublicConnections),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, NumOpenPublicConnections),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, NumMaxPrivateConnections),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, NumOpenPrivateConnections),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, JoinPolicy),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, SessionIdOverride),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, IsLANSession),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, IsDedicatedServerSession),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, bAllowNewMembers),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, bAllowSanctionedPlayers),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, bAllowUnregisteredPlayers),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, bAntiCheatProtected),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, bPresenceEnabled),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, UpdatedCustomSettings),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, RemovedCustomSettings),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, UpdatedSessionMembers),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, RemovedSessionMembers),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, UpdatedRegisteredUsers),
	ONLINE_STRUCT_FIELD(FSessionSettingsUpdate, RemovedRegisteredUsers)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FSession)
	ONLINE_STRUCT_FIELD(FSession, OwnerUserId),
	ONLINE_STRUCT_FIELD(FSession, SessionId),
	ONLINE_STRUCT_FIELD(FSession, CurrentState),
	ONLINE_STRUCT_FIELD(FSession, SessionSettings)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FSessionInvite)
	ONLINE_STRUCT_FIELD(FSessionInvite, SenderId),
	ONLINE_STRUCT_FIELD(FSessionInvite, InviteId),
	ONLINE_STRUCT_FIELD(FSessionInvite, Session)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetAllSessions::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetAllSessions::Result)
	ONLINE_STRUCT_FIELD(FGetAllSessions::Result, Sessions)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetSessionByName::Params)
	ONLINE_STRUCT_FIELD(FGetSessionByName::Params, LocalName)
	END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetSessionByName::Result)
	ONLINE_STRUCT_FIELD(FGetSessionByName::Result, Session)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetSessionById::Params)
	ONLINE_STRUCT_FIELD(FGetSessionById::Params, IdHandle)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetSessionById::Result)
	ONLINE_STRUCT_FIELD(FGetSessionById::Result, Session)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FCreateSession::Params)
	ONLINE_STRUCT_FIELD(FCreateSession::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FCreateSession::Params, SessionName),
	ONLINE_STRUCT_FIELD(FCreateSession::Params, SessionSettings),
	ONLINE_STRUCT_FIELD(FCreateSession::Params, LocalUsers)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FCreateSession::Result)
	ONLINE_STRUCT_FIELD(FCreateSession::Result, Session)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUpdateSession::Params)
	ONLINE_STRUCT_FIELD(FUpdateSession::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FUpdateSession::Params, SessionName),
	ONLINE_STRUCT_FIELD(FUpdateSession::Params, Mutations)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUpdateSession::Result)
	ONLINE_STRUCT_FIELD(FUpdateSession::Result, Session)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLeaveSession::Params)
	ONLINE_STRUCT_FIELD(FLeaveSession::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FLeaveSession::Params, SessionName),
	ONLINE_STRUCT_FIELD(FLeaveSession::Params, LocalUsers),
	ONLINE_STRUCT_FIELD(FLeaveSession::Params, bDestroySession)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLeaveSession::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FFindSessions::Params)
	ONLINE_STRUCT_FIELD(FFindSessions::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FFindSessions::Params, MaxResults),
	ONLINE_STRUCT_FIELD(FFindSessions::Params, Filters),
	ONLINE_STRUCT_FIELD(FFindSessions::Params, TargetUser),
	ONLINE_STRUCT_FIELD(FFindSessions::Params, SessionId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FFindSessions::Result)
	ONLINE_STRUCT_FIELD(FFindSessions::Result, FoundSessions)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FStartMatchmaking::Params)
	ONLINE_STRUCT_FIELD(FStartMatchmaking::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FStartMatchmaking::Params, LocalUsers),
	ONLINE_STRUCT_FIELD(FStartMatchmaking::Params, SessionName),
	ONLINE_STRUCT_FIELD(FStartMatchmaking::Params, SessionSettings),
	ONLINE_STRUCT_FIELD(FStartMatchmaking::Params, SearchFilters)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FStartMatchmaking::Result)
	ONLINE_STRUCT_FIELD(FStartMatchmaking::Result, Session)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FJoinSession::Params)
	ONLINE_STRUCT_FIELD(FJoinSession::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FJoinSession::Params, SessionName),
	ONLINE_STRUCT_FIELD(FJoinSession::Params, Session),
	ONLINE_STRUCT_FIELD(FJoinSession::Params, LocalUsers)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FJoinSession::Result)
	ONLINE_STRUCT_FIELD(FJoinSession::Result, Session)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FSendSessionInvite::Params)
	ONLINE_STRUCT_FIELD(FSendSessionInvite::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FSendSessionInvite::Params, SessionName),
	ONLINE_STRUCT_FIELD(FSendSessionInvite::Params, TargetUsers)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FSendSessionInvite::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FRejectSessionInvite::Params)
	ONLINE_STRUCT_FIELD(FRejectSessionInvite::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FRejectSessionInvite::Params, SessionInvite)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FRejectSessionInvite::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FRegisterPlayers::Params)
	ONLINE_STRUCT_FIELD(FRegisterPlayers::Params, SessionName),
	ONLINE_STRUCT_FIELD(FRegisterPlayers::Params, TargetUsers),
	ONLINE_STRUCT_FIELD(FRegisterPlayers::Params, bReserveSlot)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FRegisterPlayers::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUnregisterPlayers::Params)
	ONLINE_STRUCT_FIELD(FUnregisterPlayers::Params, SessionName),
	ONLINE_STRUCT_FIELD(FUnregisterPlayers::Params, TargetUsers),
	ONLINE_STRUCT_FIELD(FUnregisterPlayers::Params, bRemoveUnregisteredPlayers)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUnregisterPlayers::Result)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }
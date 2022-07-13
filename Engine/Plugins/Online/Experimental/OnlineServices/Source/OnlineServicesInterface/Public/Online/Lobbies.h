// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOpHandle.h"
#include "Online/CoreOnline.h"
#include "Online/OnlineMeta.h"
#include "Online/Schema.h"
#include "Misc/TVariant.h"

namespace UE::Online {

using FLobbySchemaId = FSchemaId;
using FLobbyAttributeId = FSchemaAttributeId;
using FLobbyVariant = FSchemaVariant;

enum class ELobbyJoinPolicy : uint8
{
	/** 
	* Lobby can be found through searches based on attribute matching,
	* by knowing the lobby id, or by invitation.
	*/
	PublicAdvertised,

	/** Lobby may be joined by knowing the lobby id or by invitation. */
	PublicNotAdvertised,

	/** Lobby may only be joined by invitation. */
	InvitationOnly,
};
ONLINESERVICESINTERFACE_API const TCHAR* LexToString(ELobbyJoinPolicy Policy);
ONLINESERVICESINTERFACE_API void LexFromString(ELobbyJoinPolicy& OutPolicy, const TCHAR* InStr);

enum class ELobbyMemberLeaveReason
{
	/** The lobby member explicitly left the lobby. */
	Left,

	/** The lobby member was kicked from the lobby by the lobby owner. */
	Kicked,

	/** The lobby member unexpectedly left. */
	Disconnected,

	/**
	* The lobby was destroyed by the service.
	* All members have left.
	*/
	Closed
};

enum class ELobbyAttributeVisibility
{
	Public,
	Private,
};

struct FLobbyMember
{
	FOnlineAccountIdHandle AccountId;
	FOnlineAccountIdHandle PlatformAccountId;
	FString PlatformDisplayName;
	TMap<FLobbyAttributeId, FLobbyVariant> Attributes;
};

struct FLobby
{
	FOnlineLobbyIdHandle LobbyId;
	FOnlineAccountIdHandle OwnerAccountId;
	FName LocalName;
	FName SchemaName;
	int32 MaxMembers;
	ELobbyJoinPolicy JoinPolicy;
	TMap<FLobbyAttributeId, FLobbyVariant> Attributes;
	TMap<FOnlineAccountIdHandle, TSharedRef<const FLobbyMember>> Members;
};

struct FJoinLobbyLocalUserData
{
	/** Local users who will be joining the lobby. */
	FOnlineAccountIdHandle LocalUserId;

	/** Initial attributes. */
	TMap<FLobbyAttributeId, FLobbyVariant> Attributes;
};

enum class ELobbyComparisonOp : uint8
{

	Equals,
	NotEquals,
	GreaterThan,
	GreaterThanEquals,
	LessThan,
	LessThanEquals,
	Near,
	In,
	NotIn
};

ONLINESERVICESINTERFACE_API const TCHAR* LexToString(ELobbyComparisonOp EnumVal);
ONLINESERVICESINTERFACE_API void LexFromString(ELobbyComparisonOp& OutComparison, const TCHAR* InStr);

struct FFindLobbySearchFilter
{
	/** The name of the attribute to be compared. */
	FLobbyAttributeId AttributeName;

	/** The type of comparison to perform. */
	ELobbyComparisonOp ComparisonOp;

	/** Value to use when comparing the attribute. */
	FLobbyVariant ComparisonValue;
};

ONLINESERVICESINTERFACE_API void SortLobbies(const TArray<FFindLobbySearchFilter>& Filters, TArray<TSharedRef<const FLobby>>& Lobbies);

struct FCreateLobby
{
	static constexpr TCHAR Name[] = TEXT("CreateLobby");

	/** Input struct for Lobbies::CreateLobby */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** The local name for the lobby. */
		FName LocalName;

		/** The schema which will be applied to the lobby. */
		FName SchemaName;

		/** The maximum number of members who can fit in the lobby. */
		int32 MaxMembers = 0;

		/** Initial join policy setting. */
		ELobbyJoinPolicy JoinPolicy = ELobbyJoinPolicy::InvitationOnly;

		/** Initial attributes. */
		TMap<FLobbyAttributeId, FLobbyVariant> Attributes;

		/** Local users who will be joining the lobby. */
		TArray<FJoinLobbyLocalUserData> LocalUsers;
	};

	/** Output struct for Lobbies::CreateLobby */
	struct Result
	{
		// Todo: investigate returning TSharedRef
		TSharedPtr<const FLobby> Lobby;
	};
};

struct FFindLobbies
{
	static constexpr TCHAR Name[] = TEXT("FindLobbies");

	/** Input struct for Lobbies::FindLobbies */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/**
		* Max results to return in one search.
		*  Actual count may be smaller based on implementation.
		*/
		uint32 MaxResults = 20;

		/** Filters to apply when searching for lobbies. */
		TArray<FFindLobbySearchFilter> Filters;

		/** Find lobbies containing the target user. */
		TOptional<FOnlineAccountIdHandle> TargetUser;

		/** Find join info for the target lobby id. */
		TOptional<FOnlineLobbyIdHandle> LobbyId;
	};

	/** Output struct for Lobbies::FindLobbies */
	struct Result
	{
		TArray<TSharedRef<const FLobby>> Lobbies;
	};
};

struct FRestoreLobbies
{
	static constexpr TCHAR Name[] = TEXT("RestoreLobbies");

	/** Input struct for Lobbies::RestoreLobbies */
	struct Params
	{
	};

	/** Output struct for Lobbies::RestoreLobbies */
	struct Result
	{
	};
};

struct FJoinLobby
{
	static constexpr TCHAR Name[] = TEXT("JoinLobby");

	/** Input struct for Lobbies::JoinLobby */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** The local name for the lobby. */
		FName LocalName;

		/** The id of the lobby to be joined. */
		FOnlineLobbyIdHandle LobbyId;

		/** Local users who will be joining the lobby. */
		TArray<FJoinLobbyLocalUserData> LocalUsers;
	};

	/** Output struct for Lobbies::JoinLobby */
	struct Result
	{
		// Todo: investigate returning TSharedRef
		TSharedPtr<const FLobby> Lobby;
	};
};

struct FLeaveLobby
{
	static constexpr TCHAR Name[] = TEXT("LeaveLobby");

	/** Input struct for Lobbies::LeaveLobby */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** Id of the lobby to leave. */
		FOnlineLobbyIdHandle LobbyId;
	};

	/** Output struct for Lobbies::LeaveLobby */
	struct Result
	{
	};
};

struct FInviteLobbyMember
{
	static constexpr TCHAR Name[] = TEXT("InviteLobbyMember");

	/** Input struct for Lobbies::InviteLobbyMember */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** Id of the lobby for which the invitation will be sent. */
		FOnlineLobbyIdHandle LobbyId;

		/** Id of the player who will be sent the invitation. */
		FOnlineAccountIdHandle TargetUserId;
	};

	/** Output struct for Lobbies::InviteLobbyMember */
	struct Result
	{
	};
};

struct FDeclineLobbyInvitation
{
	static constexpr TCHAR Name[] = TEXT("DeclineLobbyInvitation");

	/** Input struct for Lobbies::DeclineLobbyInvitation */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** Id of the lobby for which the invitations will be declined. */
		FOnlineLobbyIdHandle LobbyId;
	};

	/** Output struct for Lobbies::DeclineLobbyInvitation */
	struct Result
	{
	};
};

struct FKickLobbyMember
{
	static constexpr TCHAR Name[] = TEXT("KickLobbyMember");

	/** Input struct for Lobbies::KickLobbyMember */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** Id of the lobby. */
		FOnlineLobbyIdHandle LobbyId;

		/** The target user to be kicked. */
		FOnlineAccountIdHandle TargetUserId;
	};

	/** Output struct for Lobbies::KickLobbyMember */
	struct Result
	{
	};
};

struct FPromoteLobbyMember
{
	static constexpr TCHAR Name[] = TEXT("PromoteLobbyMember");

	/** Input struct for Lobbies::PromoteLobbyMember */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** Id of the lobby. */
		FOnlineLobbyIdHandle LobbyId;

		/** The target user to be promoted to owner. */
		FOnlineAccountIdHandle TargetUserId;
	};

	/** Output struct for Lobbies::PromoteLobbyMember */
	struct Result
	{
	};
};

struct FModifyLobbySchema
{
	static constexpr TCHAR Name[] = TEXT("ModifyLobbySchema");

	/** Input struct for Lobbies::ModifyLobbySchema */
	struct Params
	{
	};

	/** Output struct for Lobbies::ModifyLobbySchema */
	struct Result
	{
	};
};

struct FModifyLobbyJoinPolicy
{
	static constexpr TCHAR Name[] = TEXT("ModifyLobbyJoinPolicy");

	/** Input struct for Lobbies::ModifyLobbyJoinPolicy */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** Id of the lobby. */
		FOnlineLobbyIdHandle LobbyId;

		/** The new join policy setting. */
		ELobbyJoinPolicy JoinPolicy = ELobbyJoinPolicy::InvitationOnly;
	};

	/** Output struct for Lobbies::ModifyLobbyJoinPolicy */
	struct Result
	{
	};
};

struct FModifyLobbyAttributes
{
	static constexpr TCHAR Name[] = TEXT("ModifyLobbyAttributes");

	/** Input struct for Lobbies::ModifyLobbyAttributes */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** Id of the lobby. */
		FOnlineLobbyIdHandle LobbyId;

		/** New or changed lobby attributes. */
		TMap<FLobbyAttributeId, FLobbyVariant> MutatedAttributes;

		/** Attributes to be cleared. */
		TSet<FLobbyAttributeId> ClearedAttributes;
	};

	/** Output struct for Lobbies::ModifyLobbyAttributes */
	struct Result
	{
	};
};

struct FModifyLobbyMemberAttributes
{
	static constexpr TCHAR Name[] = TEXT("ModifyLobbyMemberAttributes");

	/** Input struct for Lobbies::ModifyLobbyMemberAttributes */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;

		/** Id of the lobby. */
		FOnlineLobbyIdHandle LobbyId;

		/** New or changed lobby attributes. */
		TMap<FLobbyAttributeId, FLobbyVariant> MutatedAttributes;

		/** Attributes to be cleared. */
		TSet<FLobbyAttributeId> ClearedAttributes;
	};

	/** Output struct for Lobbies::ModifyLobbyMemberAttributes */
	struct Result
	{
	};
};

struct FGetJoinedLobbies
{
	static constexpr TCHAR Name[] = TEXT("GetJoinedLobbies");

	/** Input struct for Lobbies::GetJoinedLobbies */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;
	};

	/** Output struct for Lobbies::GetJoinedLobbies */
	struct Result
	{
		TArray<TSharedRef<const FLobby>> Lobbies;
	};
};

struct FGetReceivedInvitations
{
	static constexpr TCHAR Name[] = TEXT("GetReceivedInvitations");

	/** Input struct for Lobbies::GetReceivedInvitations */
	struct Params
	{
		/** The local user agent which will perform the action. */
		FOnlineAccountIdHandle LocalUserId;
	};

	/** Output struct for Lobbies::GetReceivedInvitations */
	struct Result
	{
	};
};

/** Struct for LobbyJoined event */
struct FLobbyJoined
{
	/** Lobby data access. */
	TSharedRef<const FLobby> Lobby;
};

/** Struct for LobbyLeft event */
struct FLobbyLeft
{
	/** Lobby data access. */
	TSharedRef<const FLobby> Lobby;
};

/** Struct for LobbyMemberJoined event */
struct FLobbyMemberJoined
{
	/** Lobby data access. */
	TSharedRef<const FLobby> Lobby;

	/** Member data access. */
	TSharedRef<const FLobbyMember> Member;
};

/** Struct for LobbyMemberLeft event */
struct FLobbyMemberLeft
{
	/** Lobby data access. */
	TSharedRef<const FLobby> Lobby;

	/** Member data access. */
	TSharedRef<const FLobbyMember> Member;

	/** Context for the member leaving. */
	ELobbyMemberLeaveReason Reason;
};

/** Struct for LobbyLeaderChanged event */
struct FLobbyLeaderChanged
{
	/** Lobby data access. */
	TSharedRef<const FLobby> Lobby;

	/** Leader data access. */
	TSharedRef<const FLobbyMember> Leader;
};

/** Struct for LobbySchemaChanged event */
struct FLobbySchemaChanged
{
	/** Lobby data access. */
	TSharedRef<const FLobby> Lobby;
};

/** Struct for LobbyAttributesChanged event */
struct FLobbyAttributesChanged
{
	/** Lobby data access. */
	TSharedRef<const FLobby> Lobby;

	/** Attribute keys which changed. */
	TSet<FLobbyAttributeId> ChangedAttributes;

};

/** Struct for LobbyMemberAttributesChanged event */
struct FLobbyMemberAttributesChanged
{
	/** Lobby data access. */
	TSharedRef<const FLobby> Lobby;

	/** Member data access. */
	TSharedRef<const FLobbyMember> Member;

	/** Attribute keys which changed. */
	TSet<FLobbyAttributeId> ChangedAttributes;
};

/** Struct for LobbyInvitationAdded event */
struct FLobbyInvitationAdded
{
	/** The local user associated with the invitation. */
	FOnlineAccountIdHandle LocalUserId;

	/** The user who sent the invitation. */
	FOnlineAccountIdHandle SenderId;

	/** The invited lobby. */
	TSharedRef<const FLobby> Lobby;
};

/** Struct for LobbyInvitationRemoved event */
struct FLobbyInvitationRemoved
{
	/** The local user associated with the invitation. */
	FOnlineAccountIdHandle LocalUserId;

	/** The user who sent the invitation. */
	FOnlineAccountIdHandle SenderId;

	/** The invited lobby. */
	TSharedRef<const FLobby> Lobby;

};

/** Lobby join requested source */
enum class EUILobbyJoinRequestedSource : uint8
{
	/** Unspecified by the online service */
	Unspecified,
	/** From an invitation */
	FromInvitation,
};
ONLINESERVICESINTERFACE_API const TCHAR* LexToString(EUILobbyJoinRequestedSource UILobbyJoinRequestedSource);
ONLINESERVICESINTERFACE_API void LexFromString(EUILobbyJoinRequestedSource& OutUILobbyJoinRequestedSource, const TCHAR* InStr);

/** Struct for UILobbyJoinedRequested event */
struct FUILobbyJoinRequested
{
	/** The local user associated with the join request. */
	FOnlineAccountIdHandle LocalUserId;

	/** The lobby the local user requested to join, or the online error if there was a failure */
	TResult<TSharedRef<const FLobby>, FOnlineError> Result;

	/** Join request source */
	EUILobbyJoinRequestedSource JoinRequestedSource = EUILobbyJoinRequestedSource::Unspecified;
};

class ILobbies
{
public:
	//----------------------------------------------------------------------------------------------
	// Operations

	/**
	 * Create and join a new lobby.
	 *
	 * @param Params for the CreateLobby call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FCreateLobby> CreateLobby(FCreateLobby::Params&& Params) = 0;

	/**
	 * Search for lobbies using filtering parameters.
	 *
	 * @param Params for the FindLobby call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FFindLobbies> FindLobbies(FFindLobbies::Params&& Params) = 0;

	/**
	 * Try to rejoin previously joined lobbies.
	 *
	 * @param Params for the RestoreLobbies call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FRestoreLobbies> RestoreLobbies(FRestoreLobbies::Params&& Params) = 0;

	/**
	 * Join a lobby using its id.
	 *
	 * @param Params for the JoinLobby call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FJoinLobby> JoinLobby(FJoinLobby::Params&& Params) = 0;

	/**
	 * Leave a joined lobby.
	 *
	 * @param Params for the LeaveLobby call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FLeaveLobby> LeaveLobby(FLeaveLobby::Params&& Params) = 0;

	/**
	 * Invite a player to join a lobby.
	 *
	 * @param Params for the InviteLobbyMember call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FInviteLobbyMember> InviteLobbyMember(FInviteLobbyMember::Params&& Params) = 0;

	/**
	 * Decline an invitation to join a lobby.
	 *
	 * @param Params for the DeclineLobbyInvitation call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FDeclineLobbyInvitation> DeclineLobbyInvitation(FDeclineLobbyInvitation::Params&& Params) = 0;

	/**
	 * Kick a member from a the target lobby.
	 *
	 * @param Params for the KickLobbyMember call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FKickLobbyMember> KickLobbyMember(FKickLobbyMember::Params&& Params) = 0;

	/**
	 * Promote another lobby member to leader.
	 * The local user must be the current lobby leader to promote another member.
	 *
	 * @param Params for the PromoteLobbyMember call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FPromoteLobbyMember> PromoteLobbyMember(FPromoteLobbyMember::Params&& Params) = 0;

	//----------------------------------------------------------------------------------------------
	// Mutations

	/**
	 * Change the schema applied to the lobby and member attributes.
	 * Only the lobby leader may change the schema. Existing attributes not present in the new
	 * schema will be cleared.
	 *
	 * @param Params for the ModifyLobbySchema call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FModifyLobbySchema> ModifyLobbySchema(FModifyLobbySchema::Params&& Params) = 0;

	/**
	 * Change the join policy applied to the lobby.
	 * Only the lobby leader may change the join policy.
	 *
	 * @param Params for the ModifyLobbyJoinPolicy call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FModifyLobbyJoinPolicy> ModifyLobbyJoinPolicy(FModifyLobbyJoinPolicy::Params&& Params) = 0;

	/**
	 * Change the attributes applied to the lobby.
	 * Only the lobby leader may change the lobby attributes.
	 * Attributes are validated against the lobby schema before an update can succeed.
	 *
	 * @param Params for the ModifyLobbyAttributes call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FModifyLobbyAttributes> ModifyLobbyAttributes(FModifyLobbyAttributes::Params&& Params) = 0;

	/**
	 * Change the attributes applied to a lobby member.
	 * Lobby members may only change their own attributes.
	 * Attributes are validated against the lobby schema before an update can succeed.
	 *
	 * @param Params for the ModifyLobbyAttributes call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FModifyLobbyMemberAttributes> ModifyLobbyMemberAttributes(FModifyLobbyMemberAttributes::Params&& Params) = 0;

	//----------------------------------------------------------------------------------------------
	// Accessors

	/**
	 * Retrieve the list of joined lobbies for the target local user.
	 *
	 * @param Params for the GetJoinedLobbies call
	 * @return
	 */
	virtual TOnlineResult<FGetJoinedLobbies> GetJoinedLobbies(FGetJoinedLobbies::Params&& Params) = 0;

	/**
	 * Retrieve the list of received invitations for the target local user.
	 *
	 * @param Params for the GetJoinedLobbies call
	 * @return
	 */
	virtual TOnlineResult<FGetReceivedInvitations> GetReceivedInvitations(FGetReceivedInvitations::Params&& Params) = 0;

	//----------------------------------------------------------------------------------------------
	// Events

	/**
	 * Get the event that is triggered when a lobby is joined.
	 * This will happen as a result of creating or joining a lobby.
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FLobbyJoined&)> OnLobbyJoined() = 0;

	/**
	 * Get the event that is triggered when a lobby has been left by all local members.
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FLobbyLeft&)> OnLobbyLeft() = 0;

	/**
	 * Get the event that is triggered when a lobby member joins.
	 * This will happen as a result of creating or joining a lobby for local users, and will trigger
	 * when a remote user joins.
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FLobbyMemberJoined&)> OnLobbyMemberJoined() = 0;

	/**
	 * Get the event that is triggered when a lobby member leaves a joined lobby.
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FLobbyMemberLeft&)> OnLobbyMemberLeft() = 0;

	/**
	 * Get the event that is triggered when the leadership of a lobby changes.
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FLobbyLeaderChanged&)> OnLobbyLeaderChanged() = 0;

	/**
	 * Get the event that is triggered when the attribute schema of a lobby changes.
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FLobbySchemaChanged&)> OnLobbySchemaChanged() = 0;

	/**
	 * Get the event that is triggered when lobby attributes have changed.
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FLobbyAttributesChanged&)> OnLobbyAttributesChanged() = 0;

	/**
	 * Get the event that is triggered when lobby member attributes have changed.
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FLobbyMemberAttributesChanged&)> OnLobbyMemberAttributesChanged() = 0;

	/**
	 * Get the event that is triggered when an invitation is received.
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FLobbyInvitationAdded&)> OnLobbyInvitationAdded() = 0;

	/**
	 * Get the event that is triggered when an invitation is removed.
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FLobbyInvitationRemoved&)> OnLobbyInvitationRemoved() = 0;

	/**
	 * Get the event that is triggered when a join is requested through an external mechanism.
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FUILobbyJoinRequested&)> OnUILobbyJoinRequested() = 0;
};

namespace Meta {
// TODO: Move to Lobbies_Meta.inl file?

BEGIN_ONLINE_STRUCT_META(FLobby)
	ONLINE_STRUCT_FIELD(FLobby, LobbyId),
	ONLINE_STRUCT_FIELD(FLobby, LocalName),
	ONLINE_STRUCT_FIELD(FLobby, SchemaName),
	ONLINE_STRUCT_FIELD(FLobby, MaxMembers),
	ONLINE_STRUCT_FIELD(FLobby, JoinPolicy)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FJoinLobbyLocalUserData)
	ONLINE_STRUCT_FIELD(FJoinLobbyLocalUserData, LocalUserId),
	ONLINE_STRUCT_FIELD(FJoinLobbyLocalUserData, Attributes)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FCreateLobby::Params)
	ONLINE_STRUCT_FIELD(FCreateLobby::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FCreateLobby::Params, LocalName),
	ONLINE_STRUCT_FIELD(FCreateLobby::Params, SchemaName),
	ONLINE_STRUCT_FIELD(FCreateLobby::Params, MaxMembers),
	ONLINE_STRUCT_FIELD(FCreateLobby::Params, JoinPolicy),
	ONLINE_STRUCT_FIELD(FCreateLobby::Params, Attributes),
	ONLINE_STRUCT_FIELD(FCreateLobby::Params, LocalUsers)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FCreateLobby::Result)
	ONLINE_STRUCT_FIELD(FCreateLobby::Result, Lobby)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FFindLobbies::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FFindLobbies::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FRestoreLobbies::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FRestoreLobbies::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FJoinLobby::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FJoinLobby::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLeaveLobby::Params)
	ONLINE_STRUCT_FIELD(FLeaveLobby::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FLeaveLobby::Params, LobbyId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLeaveLobby::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FInviteLobbyMember::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FInviteLobbyMember::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FDeclineLobbyInvitation::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FDeclineLobbyInvitation::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FKickLobbyMember::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FKickLobbyMember::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FPromoteLobbyMember::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FPromoteLobbyMember::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FModifyLobbySchema::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FModifyLobbySchema::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FModifyLobbyJoinPolicy::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FModifyLobbyJoinPolicy::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FModifyLobbyAttributes::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FModifyLobbyAttributes::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FModifyLobbyMemberAttributes::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FModifyLobbyMemberAttributes::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetJoinedLobbies::Params)
	ONLINE_STRUCT_FIELD(FGetJoinedLobbies::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetJoinedLobbies::Result)
	ONLINE_STRUCT_FIELD(FGetJoinedLobbies::Result, Lobbies)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetReceivedInvitations::Params)
	ONLINE_STRUCT_FIELD(FGetReceivedInvitations::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetReceivedInvitations::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbyJoined)
	ONLINE_STRUCT_FIELD(FLobbyJoined, Lobby)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbyLeft)
	ONLINE_STRUCT_FIELD(FLobbyLeft, Lobby)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbyMemberJoined)
	ONLINE_STRUCT_FIELD(FLobbyMemberJoined, Lobby),
	ONLINE_STRUCT_FIELD(FLobbyMemberJoined, Member)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbyMemberLeft)
	ONLINE_STRUCT_FIELD(FLobbyMemberLeft, Lobby),
	ONLINE_STRUCT_FIELD(FLobbyMemberLeft, Member),
	ONLINE_STRUCT_FIELD(FLobbyMemberLeft, Reason)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbyLeaderChanged)
	ONLINE_STRUCT_FIELD(FLobbyLeaderChanged, Lobby),
	ONLINE_STRUCT_FIELD(FLobbyLeaderChanged, Leader)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbyAttributesChanged)
	ONLINE_STRUCT_FIELD(FLobbyAttributesChanged, Lobby),
	ONLINE_STRUCT_FIELD(FLobbyAttributesChanged, ChangedAttributes)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbyMemberAttributesChanged)
	ONLINE_STRUCT_FIELD(FLobbyMemberAttributesChanged, Lobby),
	ONLINE_STRUCT_FIELD(FLobbyMemberAttributesChanged, Member),
	ONLINE_STRUCT_FIELD(FLobbyMemberAttributesChanged, ChangedAttributes)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbyInvitationAdded)
	ONLINE_STRUCT_FIELD(FLobbyInvitationAdded, LocalUserId),
	ONLINE_STRUCT_FIELD(FLobbyInvitationAdded, SenderId),
	ONLINE_STRUCT_FIELD(FLobbyInvitationAdded, Lobby)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbyInvitationRemoved)
	ONLINE_STRUCT_FIELD(FLobbyInvitationRemoved, LocalUserId),
	ONLINE_STRUCT_FIELD(FLobbyInvitationRemoved, SenderId),
	ONLINE_STRUCT_FIELD(FLobbyInvitationRemoved, Lobby)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUILobbyJoinRequested)
	ONLINE_STRUCT_FIELD(FUILobbyJoinRequested, LocalUserId),
	ONLINE_STRUCT_FIELD(FUILobbyJoinRequested, Result)
END_ONLINE_STRUCT_META()


/* Meta*/ }

/* UE::Online */ }

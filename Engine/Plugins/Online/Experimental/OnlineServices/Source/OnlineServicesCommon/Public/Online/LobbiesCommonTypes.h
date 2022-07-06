// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "Online/Lobbies.h"

namespace UE::Online {

#define LOBBIES_FUNCTIONAL_TEST_ENABLED !UE_BUILD_SHIPPING

class FOnlineServicesCommon;
class FAccountInfo;

struct FLobbyConfig
{
	TArray<FLobbySchemaId> RegisteredSchemas;
};

struct FLobbySchemaAttributeConfig
{
	// The attribute name.
	FLobbyAttributeId Name;
	
	// Override default service attribute field assignment.
	// Specifying an override allows custom grouping of attributes within a
	// platform attribute. This grouping is useful when a set of attributes are
	// known to update at the same cadence.
	TOptional<FLobbyAttributeId> ServiceAttributeFieldId;
	
	// The size in bytes of the attribute value.
	// A schema will fail validation if all attributes will not fit within the
	// platform's lobby fields.
	uint32 MaxByteSize = 0;

	// Control whether the attribute is visible to players who are not joined
	// to the lobby.
	ELobbyAttributeVisibility Visibility = ELobbyAttributeVisibility::Private;

};

struct FLobbySchemaConfig
{
	// The schema name.
	FLobbySchemaId SchemaName;

	// The optional base schema name.
	FLobbySchemaId BaseSchemaName;
	
	// The definitions for attributes attached to a lobby.
	TArray<FLobbySchemaAttributeConfig> LobbyAttributes;

	// The definitions for attributes attached to a lobby member.
	TArray<FLobbySchemaAttributeConfig> LobbyMemberAttributes;
};

class FLobbyServiceAttributeChanges final
{
public:
	TMap<FLobbyAttributeId, FLobbyVariant> MutatedAttributes;
	TSet<FLobbyAttributeId> ClearedAttributes;
};

class FLobbyClientAttributeChanges final
{
public:
	// Apply attribute changes to attribute parameter.
	// Applying the changes will clear MutatedAttributes and ClearedAttributes.
	// Returns a list of the attributes which were changed.
	TSet<FLobbyAttributeId> Apply(TMap<FLobbyAttributeId, FLobbyVariant>& InOutAttributes)
	{
		TSet<FLobbyAttributeId> Changes;
		Changes.Reserve(MutatedAttributes.Num() + ClearedAttributes.Num());

		for (TPair<FLobbyAttributeId, FLobbyVariant>& Attribute : MutatedAttributes)
		{
			Changes.Add(Attribute.Key);
			InOutAttributes.Emplace(Attribute.Key, MoveTemp(Attribute.Value));
		}

		for (FLobbyAttributeId AttributeId : ClearedAttributes)
		{
			Changes.Add(AttributeId);
			InOutAttributes.Remove(AttributeId);
		}

		MutatedAttributes.Empty();
		ClearedAttributes.Empty();

		return Changes;
	}

	TMap<FLobbyAttributeId, FLobbyVariant> MutatedAttributes;
	TSet<FLobbyAttributeId> ClearedAttributes;
};

struct FLobbyEvents final
{
	TOnlineEventCallable<void(const FLobbyJoined&)> OnLobbyJoined;
	TOnlineEventCallable<void(const FLobbyLeft&)> OnLobbyLeft;
	TOnlineEventCallable<void(const FLobbyMemberJoined&)> OnLobbyMemberJoined;
	TOnlineEventCallable<void(const FLobbyMemberLeft&)> OnLobbyMemberLeft;
	TOnlineEventCallable<void(const FLobbyLeaderChanged&)> OnLobbyLeaderChanged;
	TOnlineEventCallable<void(const FLobbySchemaChanged&)> OnLobbySchemaChanged;
	TOnlineEventCallable<void(const FLobbyAttributesChanged&)> OnLobbyAttributesChanged;
	TOnlineEventCallable<void(const FLobbyMemberAttributesChanged&)> OnLobbyMemberAttributesChanged;
	TOnlineEventCallable<void(const FLobbyInvitationAdded&)> OnLobbyInvitationAdded;
	TOnlineEventCallable<void(const FLobbyInvitationRemoved&)> OnLobbyInvitationRemoved;
	TOnlineEventCallable<void(const FUILobbyJoinRequested&)> OnUILobbyJoinRequested;
};

class ONLINESERVICESCOMMON_API FLobbySchema final
{
public:
	static TSharedPtr<FLobbySchema> Create(FLobbySchemaConfig LobbySchemaConfig);

	TSharedRef<FLobbyServiceAttributeChanges> TranslateLobbyAttributes(const TMap<FLobbyAttributeId, FLobbyVariant>& LobbyAttributes, const FLobbyClientAttributeChanges& ClientAttributeChanges) const;
	TSharedRef<FLobbyClientAttributeChanges> TranslateLobbyAttributes(const FLobbyServiceAttributeChanges& ServiceAttributeChanges) const;
	TSharedRef<FLobbyServiceAttributeChanges> TranslateLobbyMemberAttributes(const TMap<FLobbyAttributeId, FLobbyVariant>& LobbyAttributes, const FLobbyClientAttributeChanges& ClientAttributeChanges) const;
	TSharedRef<FLobbyClientAttributeChanges> TranslateLobbyMemberAttributes(const FLobbyServiceAttributeChanges& ServiceAttributeChanges) const;

private:
	FLobbySchemaId SchemaId;
};

class FLobbySchemaRegistry
{
public:
	bool Initialize(TArray<FLobbySchemaConfig> LobbySchemaConfigs);
	TSharedPtr<FLobbySchema> FindSchema(FLobbySchemaId SchemaId);

private:
	bool RegisterSchema(FLobbySchemaConfig LobbySchemaConfig);

	TMap<FLobbySchemaId, TSharedRef<FLobbySchema>> RegisteredSchemas;
};

// Todo: put this somewhere else
template <typename AwaitedType>
TFuture<TArray<AwaitedType>> WhenAll(TArray<TFuture<AwaitedType>>&& Futures)
{
	struct FWhenAllState
	{
		TArray<TFuture<AwaitedType>> Futures;
		TArray<AwaitedType> Results;
		TPromise<TArray<AwaitedType>> FinalPromise;
	};

	if (Futures.IsEmpty())
	{
		return MakeFulfilledPromise<TArray<AwaitedType>>().GetFuture();
	}
	else
	{
		TSharedRef<FWhenAllState> WhenAllState = MakeShared<FWhenAllState>();
		WhenAllState->Futures = MoveTemp(Futures);

		for (TFuture<AwaitedType>& Future : WhenAllState->Futures)
		{
			Future.Then([WhenAllState](TFuture<AwaitedType>&& AwaitedResult)
			{
				WhenAllState->Results.Emplace(MoveTempIfPossible(AwaitedResult.Get()));

				if (WhenAllState->Futures.Num() == WhenAllState->Results.Num())
				{
					WhenAllState->FinalPromise.EmplaceValue(MoveTemp(WhenAllState->Results));
				}
			});
		}

		return WhenAllState->FinalPromise.GetFuture();
	}
}

/**
 * Local changes to a member to be applied to an existing lobby member snapshot.
 */
struct FClientLobbyMemberDataChanges
{
	/** New or changed attributes. */
	TMap<FLobbyAttributeId, FLobbyVariant> MutatedAttributes;

	/** Attributes to be cleared. */
	TSet<FLobbyAttributeId> ClearedAttributes;
};

/**
 * Local changes to a lobby to be applied to an existing lobby snapshot.
 */
struct FClientLobbyDataChanges
{
	/** Local name for the lobby. */
	TOptional<FName> LocalName;

	/** Setting for new join policy. */
	TOptional<ELobbyJoinPolicy> JoinPolicy;

	/** Setting for lobby ownership change. */
	TOptional<FOnlineAccountIdHandle> OwnerAccountId;

	/** Setting for lobby schema change. */
	TOptional<FLobbySchemaId> LobbySchema;

	/** New or changed attributes. */
	TMap<FLobbyAttributeId, FLobbyVariant> MutatedAttributes;

	/** Attributes to be cleared. */
	TSet<FLobbyAttributeId> ClearedAttributes;

	/** Members to be added or changed. */
	TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberDataChanges>> MutatedMembers;

	/** Members to be removed. */
	TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeavingMembers;
};

/**
* Lobby snapshot data.
* All contained data has been translated from EOS types.
* Attributes have had their schema transformations applied.
*/
struct FClientLobbySnapshot
{
	FOnlineAccountIdHandle OwnerAccountId;
	FName SchemaName;
	int32 MaxMembers;
	ELobbyJoinPolicy JoinPolicy;
	TMap<FLobbyAttributeId, FLobbyVariant> Attributes;
	TSet<FOnlineAccountIdHandle> Members;
};

struct FClientLobbyMemberSnapshot : public FLobbyMember
{
	bool bIsLocalMember = false;
};

struct FApplyLobbyUpdateResult
{
	TArray<FOnlineAccountIdHandle> LeavingLocalMembers;
};

/** Lobby data as seen by the client. */
struct ONLINESERVICESCOMMON_API FClientLobbyData final
{
public:
	FClientLobbyData(FOnlineLobbyIdHandle LobbyId);

	TSharedRef<const FLobby> GetPublicDataPtr() const { return PublicData; }
	const FLobby& GetPublicData() const { return *PublicData; }

	TSharedPtr<const FClientLobbyMemberSnapshot> GetMemberData(FOnlineAccountIdHandle MemberAccountId) const;

	/**
	 * Apply updated lobby data and generate changes.
	 * The LeaveReason provides context for members who have left since the most recent snapshot.
	 * Changing a lobby generates events for the local client.
	 */
	FApplyLobbyUpdateResult ApplyLobbyUpdateFromServiceSnapshot(
		FClientLobbySnapshot&& LobbySnapshot,
		TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>>&& LobbyMemberSnapshots,
		TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason>&& LeaveReasons = TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason>(),
		FLobbyEvents* LobbyEvents = nullptr);

	/**
	 * Apply updated lobby data and generate changes.
	 * Changing a lobby generates events for the local client.
	 */
	FApplyLobbyUpdateResult ApplyLobbyUpdateFromLocalChanges(FClientLobbyDataChanges&& Changes, FLobbyEvents& LobbyEvents);

private:

	/** Apply changes to a set of attributes. Returns the set of attribute IDs which changed. */
	TSet<FLobbyAttributeId> ApplyAttributeUpdateFromSnapshot(
		TMap<FLobbyAttributeId, FLobbyVariant>&& AttributeSnapshot,
		TMap<FLobbyAttributeId, FLobbyVariant>& ExistingAttributes);

	/** Apply changes to a set of attributes. Returns the set of attribute IDs which changed. */
	TSet<FLobbyAttributeId> ApplyAttributeUpdateFromChanges(
		TMap<FLobbyAttributeId, FLobbyVariant>&& MutatedAttributes,
		TSet<FLobbyAttributeId>&& ClearedAttributes,
		TMap<FLobbyAttributeId, FLobbyVariant>& ExistingAttributes);

	/**
	 * The shared pointer given back to user code with lobby operation results and notifications.
	 * Any changes to this data are immediately available to users.
	 */
	TSharedRef<FLobby> PublicData;

	/** Mutable lobby member data storage. */
	TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> MemberDataStorage;

	/**
	 * Keep track of which members are local to the client.
	 * When all local members have been removed all members will be removed.
	 */
	TSet<FOnlineAccountIdHandle> LocalMembers;
};

#if LOBBIES_FUNCTIONAL_TEST_ENABLED
struct FFunctionalTestLobbies
{
	static constexpr TCHAR Name[] = TEXT("FunctionalTest");

	struct Params
	{
	};

	struct Result
	{
	};
};
#endif // LOBBIES_FUNCTIONAL_TEST_ENABLED

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FLobbyConfig)
	ONLINE_STRUCT_FIELD(FLobbyConfig, RegisteredSchemas)
END_ONLINE_STRUCT_META()


BEGIN_ONLINE_STRUCT_META(FLobbySchemaAttributeConfig)
	ONLINE_STRUCT_FIELD(FLobbySchemaAttributeConfig, Name),
	ONLINE_STRUCT_FIELD(FLobbySchemaAttributeConfig, ServiceAttributeFieldId),
	ONLINE_STRUCT_FIELD(FLobbySchemaAttributeConfig, MaxByteSize),
	ONLINE_STRUCT_FIELD(FLobbySchemaAttributeConfig, Visibility)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbySchemaConfig)
	ONLINE_STRUCT_FIELD(FLobbySchemaConfig, BaseSchemaName),
	ONLINE_STRUCT_FIELD(FLobbySchemaConfig, LobbyAttributes),
	ONLINE_STRUCT_FIELD(FLobbySchemaConfig, LobbyMemberAttributes)
END_ONLINE_STRUCT_META()

#if LOBBIES_FUNCTIONAL_TEST_ENABLED
BEGIN_ONLINE_STRUCT_META(FFunctionalTestLobbies::Params)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FFunctionalTestLobbies::Result)
END_ONLINE_STRUCT_META()
#endif // LOBBIES_FUNCTIONAL_TEST_ENABLED

/* Meta */ }

/* UE::Online */ }

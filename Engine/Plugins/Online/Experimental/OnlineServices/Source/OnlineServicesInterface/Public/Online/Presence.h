// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/CoreOnline.h"
#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineMeta.h"

namespace UE::Online {

using FPresenceVariant = FString; // TODO:  Import FVariantData

enum class EPresenceState : uint8
{
	/** User is offline */
	Offline,
	/** User is online */
	Online,
	/** User is away */
	Away,
	/** User is extended away */
	ExtendedAway,
	/** User is in do not disturb mode */
	DoNotDisturb
};

typedef TMap<FString, FPresenceVariant> FPresenceProperties;

struct FUserPresence
{
	/** User whose presence this is */
	FOnlineAccountIdHandle UserId;
	/** Presence state */
	EPresenceState State;
	/** Presence string */
	FString StatusString;
	/** Presence properties */
	FPresenceProperties Properties;
};
	
struct FQueryPresence
{
	static constexpr TCHAR Name[] = TEXT("QueryPresence");

	struct Params
	{
		/** Local user performing the query */
		FOnlineAccountIdHandle LocalUserId;
		/** User to query the presence for */
		FOnlineAccountIdHandle TargetUserId;
	};

	struct Result
	{
		/** The retrieved presence */
		TSharedRef<const FUserPresence> Presence;
	};
};

struct FGetPresence
{
	static constexpr TCHAR Name[] = TEXT("GetPresence");

	struct Params
	{
		/** Local user getting the presence */
		FOnlineAccountIdHandle LocalUserId;
		/** User to get the presence for */
		FOnlineAccountIdHandle TargetUserId;
	};

	struct Result
	{
		/** The presence */
		TSharedRef<const FUserPresence> Presence;
	};
};

struct FUpdatePresence
{
	static constexpr TCHAR Name[] = TEXT("UpdatePresence");

	struct Params
	{
		/** Local user performing the query */
		FOnlineAccountIdHandle LocalUserId;
		/** Mutations */
		struct FMutations
		{
			/** Presence state */
			TOptional<EPresenceState> State;
			/** Status string */
			TOptional<FString> StatusString;
			/** Presence keys to update */
			FPresenceProperties UpdatedProperties;
			/** Properties to remove */
			TArray<FString> RemovedProperties;

			FMutations& operator+=(FMutations&& NewParams);
		};
		/** Mutations */
		FMutations Mutations;
	};

	struct Result
	{
	};
};

/** Struct for PresenceUpdated event */
struct FPresenceUpdated
{
	/** Local user receiving the presence update */
	FOnlineAccountIdHandle LocalUserId;
	/** Presence that has updated */
	TSharedRef<const FUserPresence> UpdatedPresence;
};

class IPresence
{
public:
	/**
	 * Query presence for a user
	 * 
	 * @param Params for the QueryPresence call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FQueryPresence> QueryPresence(FQueryPresence::Params&& Params) = 0;

	/**
	 * Get the presence of a user
	 * Presence typically comes from QueryPresence or push events from the online service
	 *
	 * @param Params for the GetPresence call
	 * @return
	 */
	virtual TOnlineResult<FGetPresence> GetPresence(FGetPresence::Params&& Params) = 0;

	/**
	 * Update your presence
	 *
	 * @param Params for the UpdatePresence call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FUpdatePresence> UpdatePresence(FUpdatePresence::Params&& Params) = 0;

	/**
	 * Get the event that is triggered when presence is updated
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FPresenceUpdated&)> OnPresenceUpdated() = 0;
};

inline FUpdatePresence::Params::FMutations& FUpdatePresence::Params::FMutations::operator+=(FUpdatePresence::Params::FMutations&& NewParams)
{
	if (NewParams.State.IsSet())
	{
		State = NewParams.State;
	}

	if (NewParams.StatusString.IsSet())
	{
		StatusString = MoveTemp(NewParams.StatusString);
	}

	// Insert any updated keys / remove from removed
	for (TPair<FString, FPresenceVariant>& NewUpdatedProperty : NewParams.UpdatedProperties)
	{
		RemovedProperties.Remove(NewUpdatedProperty.Key);
		UpdatedProperties.Emplace(MoveTemp(NewUpdatedProperty.Key), MoveTemp(NewUpdatedProperty.Value));
	}

	// Merge any removed keys / remove from updated
	RemovedProperties.Reserve(RemovedProperties.Num() + NewParams.RemovedProperties.Num());
	for (FString& NewRemovedKey : NewParams.RemovedProperties)
	{
		UpdatedProperties.Remove(NewRemovedKey);
		RemovedProperties.AddUnique(MoveTemp(NewRemovedKey));
	}
	return *this;
}

namespace Meta {
// TODO: Move to Presence_Meta.inl file?

BEGIN_ONLINE_STRUCT_META(FQueryPresence::Params)
	ONLINE_STRUCT_FIELD(FQueryPresence::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FQueryPresence::Params, TargetUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FQueryPresence::Result)
	ONLINE_STRUCT_FIELD(FQueryPresence::Result, Presence)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetPresence::Params)
	ONLINE_STRUCT_FIELD(FGetPresence::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetPresence::Result)
	ONLINE_STRUCT_FIELD(FGetPresence::Result, Presence)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUpdatePresence::Params)
	ONLINE_STRUCT_FIELD(FUpdatePresence::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FUpdatePresence::Params, Mutations)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUpdatePresence::Params::FMutations)
	ONLINE_STRUCT_FIELD(FUpdatePresence::Params::FMutations, State),
	ONLINE_STRUCT_FIELD(FUpdatePresence::Params::FMutations, StatusString),
	ONLINE_STRUCT_FIELD(FUpdatePresence::Params::FMutations, UpdatedProperties),
	ONLINE_STRUCT_FIELD(FUpdatePresence::Params::FMutations, RemovedProperties)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUpdatePresence::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FPresenceUpdated)
	ONLINE_STRUCT_FIELD(FPresenceUpdated, LocalUserId),
	ONLINE_STRUCT_FIELD(FPresenceUpdated, UpdatedPresence)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }

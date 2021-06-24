// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineId.h"
#include "Online/OnlineMeta.h"

namespace UE::Online {

struct FQueryFriends
{
	/** Input struct for Friends::QueryFriends */
	struct Params
	{
		/** Account Id of the local user making the request */
		FAccountId LocalUserId;
	};

	/** Output struct for Friends::QueryFriends */
	struct Result
	{
	};
};

struct FGetFriends
{
	/** Input struct for Friends::GetFriends */
	struct Params
	{
		/** Account Id of the local user making the request */
		FAccountId LocalUserId;
	};

	/** Output struct for Friends::GetFriends */
	struct Result
	{
		/** Array of friend account ids */
		TArray<FAccountId> FriendIds;
	};
};

/** Struct for FriendsListUpdate event */
struct FFriendsListUpdated
{
	/** Local user who's friends list changed */
	FOnlineId LocalUserId;
};

class IFriends
{
public:
	/**
	 * Query the friends list
	 * 
	 * @param Params for the QueryFriends call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FQueryFriends> QueryFriends(FQueryFriends::Params&& Params) = 0;

	/**
	 * Get the contents of a previously queried friends list
	 *
	 * @param Params for the GetFriends call
	 * @return
	 */
	virtual TOnlineResult<FGetFriends::Result> GetFriends(FGetFriends::Params&& Params) = 0;

	/**
	 * Get the event that is triggered when a friends list is updated
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FFriendsListUpdated&)> OnFriendsListUpdated() = 0;
};

namespace Meta {
// TODO: Move to Friends_Meta.inl file?

BEGIN_ONLINE_STRUCT_META(FQueryFriends::Params)
	ONLINE_STRUCT_FIELD(FQueryFriends::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FQueryFriends::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetFriends::Params)
	ONLINE_STRUCT_FIELD(FGetFriends::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetFriends::Result)
	ONLINE_STRUCT_FIELD(FGetFriends::Result, FriendIds)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FFriendsListUpdated)
	ONLINE_STRUCT_FIELD(FFriendsListUpdated, LocalUserId)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }

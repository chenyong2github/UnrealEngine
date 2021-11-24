// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineId.h"
#include "Online/OnlineMeta.h"

namespace UE::Online {

class FOnlineError;

struct FExternalUIShowFriendsUI
{
	static constexpr TCHAR Name[] = TEXT("ShowFriendsUI");

	struct Params
	{
		FOnlineAccountIdHandle LocalUserId;
	};

	struct Result
	{
	};
};

class IExternalUI
{
public:
	/**
	 * Shows the friends tab of the EOS Overlay if enabled
	 */
	virtual TOnlineAsyncOpHandle<FExternalUIShowFriendsUI> ShowFriendsUI(FExternalUIShowFriendsUI::Params&& Params) = 0;
};

namespace Meta {
// TODO: Move to ExternalUI_Meta.inl file?

BEGIN_ONLINE_STRUCT_META(FExternalUIShowFriendsUI::Params)
	ONLINE_STRUCT_FIELD(FExternalUIShowFriendsUI::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FExternalUIShowFriendsUI::Result)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }

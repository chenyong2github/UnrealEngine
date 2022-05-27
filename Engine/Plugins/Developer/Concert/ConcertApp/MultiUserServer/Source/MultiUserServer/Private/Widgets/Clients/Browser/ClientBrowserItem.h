// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessageData.h"
#include "IMessageContext.h"

namespace UE::MultiUserServer
{
	struct FClientBrowserItem
	{
		FConcertSessionClientInfo ClientInfo;
		FMessageAddress ClientAddress;

		FClientBrowserItem(const FConcertSessionClientInfo& ClientInfo, FMessageAddress ClientAddress)
			: ClientInfo(ClientInfo)
			, ClientAddress(ClientAddress)
		{}
	};
}

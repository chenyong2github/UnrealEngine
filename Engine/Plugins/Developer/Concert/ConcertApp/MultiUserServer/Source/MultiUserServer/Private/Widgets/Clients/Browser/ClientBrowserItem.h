// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessageData.h"
#include "IMessageContext.h"

namespace UE::MultiUserServer
{
	DECLARE_DELEGATE_RetVal(TOptional<FConcertClientInfo>, FGetClientInfo);
	
	struct FClientBrowserItem
	{
		/** Getter for getting the latest client info. */
		FGetClientInfo GetClientInfo;
		
		/** Address of this client as used by the UDP messaging sytem */
		FMessageAddress ClientAddress;
		/** ID of this client in the UDP messaging system */
		FGuid MessageNodeId;

		/** The session this client currently is in */
		TOptional<FGuid> CurrentSession;

		/** Whether this client is no longer connected with the server */
		bool bIsDisconnected = false;

		FClientBrowserItem(FGetClientInfo GetClientInfo, const FMessageAddress& ClientAddress, const FGuid& MessageNodeId, TOptional<FGuid> CurrentSession = {})
			: GetClientInfo(MoveTemp(GetClientInfo))
			, ClientAddress(ClientAddress)
			, MessageNodeId(MessageNodeId)
			, CurrentSession(CurrentSession)
		{}
	};
}

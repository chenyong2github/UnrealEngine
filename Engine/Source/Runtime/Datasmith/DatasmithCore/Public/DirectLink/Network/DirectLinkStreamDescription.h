// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/Network/DirectLinkStreamSender.h"
#include "DirectLink/Network/DirectLinkStreamReceiver.h"

#include "CoreTypes.h"
#include "IMessageContext.h"



namespace DirectLink
{



struct FStreamDescription
{
	enum class EConnectionState
	{
		Uninitialized,
		RequestSent,
		Active,
		Closed,
	};

	bool bThisIsSource = false;

	FGuid SourcePoint;
	FGuid DestinationPoint;
	FStreamPort LocalStreamPort = 0; // works like an Id within that endpoint
	FMessageAddress RemoteAddress;
	FStreamPort RemoteStreamPort = 0;
	EConnectionState Status = EConnectionState::Uninitialized;
	double LastRemoteLifeSign = 0; // as returned by FPlatformTime::Seconds()
	 // #ue_directlink_streams implement old connection pruning
	TUniquePtr<FStreamReceiver> Receiver; // #ue_directlink_cleanup not required outside of the internal thread
	TSharedPtr<FStreamSender> Sender;
};

} // namespace DirectLink

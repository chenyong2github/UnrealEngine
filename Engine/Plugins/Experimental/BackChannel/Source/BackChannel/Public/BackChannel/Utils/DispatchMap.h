// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BackChannel/Types.h"



class BACKCHANNEL_API FBackChannelDispatchMap
{

public:

	FBackChannelDispatchMap();

	virtual ~FBackChannelDispatchMap() {}

	FDelegateHandle AddRoute(FStringView Path, FBackChannelRouteDelegate::FDelegate Delegate);
	
	void RemoveRoute(FStringView Path, FDelegateHandle DelegateHandle);

	void	DispatchMessage(IBackChannelPacket& Message);

protected:

	TMap<FString, FBackChannelRouteDelegate> DispatchMap;

};
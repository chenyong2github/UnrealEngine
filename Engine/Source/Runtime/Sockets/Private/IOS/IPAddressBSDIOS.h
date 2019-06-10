// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BSDSockets/SocketSubsystemBSDPrivate.h"
#include "BSDSockets/IPAddressBSD.h"

#if PLATFORM_HAS_BSD_IPV6_SOCKETS

class FInternetAddrBSDIOS : public FInternetAddrBSD
{

public:

	FInternetAddrBSDIOS(FSocketSubsystemBSD* InSocketSubsystem) : FInternetAddrBSD(InSocketSubsystem) {}

	/** Sets the address to broadcast */
	virtual void SetIPv6BroadcastAddress() override
	{
		FSocketSubsystemBSD* SocketSubsystem = static_cast<FSocketSubsystemBSD*>(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM));
		if (SocketSubsystem)
		{
			TSharedPtr<FInternetAddrBSD> MultiCastAddr = StaticCastSharedPtr<FInternetAddrBSD>(SocketSubsystem->GetAddressFromString(TEXT("ff02::01")));
			if (!MultiCastAddr.IsValid())
			{
				UE_LOG(LogSockets, Warning, TEXT("Could not resolve the broadcast address for iOS, this address will just be blank"));
			}
			else
			{
				// Set the address from the query
				SetRawIp(MultiCastAddr->GetRawIp());

				// Do a query to get the scope id of the address.
				bool bUnusedBool;
				TSharedRef<FInternetAddrBSDIOS> ScopeAddr = 
					StaticCastSharedRef<FInternetAddrBSDIOS>(SocketSubsystem->GetLocalHostAddr(*GLog, bUnusedBool));
				SetScopeId(ScopeAddr->GetScopeId());
			}
		}
		else
		{
			UE_LOG(LogSockets, Warning, TEXT("Could not get the socketsubsystem for querying the scope id of the broadcast address"));
		}
		SetPort(0);
	}
};

#endif

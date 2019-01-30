// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BSDSockets/SocketSubsystemBSDPrivate.h"
#include "Sockets.h"
#include "BSDSockets/SocketsBSD.h"

class FInternetAddr;

/**
 * Implements a BSD network socket on IOS.
 */
class FSocketBSDIOS
	: public FSocketBSD
{
public:

	FSocketBSDIOS(SOCKET InSocket, ESocketType InSocketType, const FString& InSocketDescription, const FName& InSocketProtocol, ISocketSubsystem* InSubsystem)
		:FSocketBSD(InSocket, InSocketType, InSocketDescription, InSocketProtocol, InSubsystem)
	{
	}

	virtual ~FSocketBSDIOS()
	{	
		FSocketBSD::Close();
	}

};

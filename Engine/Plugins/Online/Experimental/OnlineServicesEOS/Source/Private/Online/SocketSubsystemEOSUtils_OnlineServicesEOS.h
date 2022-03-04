// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_ENGINE

#include "SocketSubsystemEOS.h"

namespace UE::Online {

class FOnlineServicesEOS;

class FSocketSubsystemEOSUtils_OnlineServicesEOS : public ISocketSubsystemEOSUtils
{
public:
	FSocketSubsystemEOSUtils_OnlineServicesEOS(FOnlineServicesEOS& InServicesEOS);
	virtual ~FSocketSubsystemEOSUtils_OnlineServicesEOS() override;

	virtual EOS_ProductUserId GetLocalUserId() override;
	virtual FString GetSessionId() override;
	virtual FName GetSubsystemInstanceName() override;

private:
	FOnlineServicesEOS& ServicesEOS;
};

/* UE::Online */}

#endif // WITH_ENGINE
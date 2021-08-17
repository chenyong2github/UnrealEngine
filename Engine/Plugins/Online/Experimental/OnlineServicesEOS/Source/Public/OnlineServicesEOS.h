// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineServicesCommon.h"

#if WITH_EOS_SDK

using IEOSPlatformHandlePtr = TSharedPtr<class IEOSPlatformHandle>;

#include "eos_sdk.h"

namespace UE::Online {

class ONLINESERVICESEOS_API FOnlineServicesEOS : public FOnlineServicesCommon
{
public:
	FOnlineServicesEOS();
	virtual void RegisterComponents() override;

	static EOnlineServices GetServicesProvider() { return EOnlineServices::Epic; }

	EOS_HPlatform GetEOSPlatformHandle() const;
protected:
	IEOSPlatformHandlePtr EOSPlatformHandle;
};

/* UE::Online */ }

#endif // WITH_EOS_SDK
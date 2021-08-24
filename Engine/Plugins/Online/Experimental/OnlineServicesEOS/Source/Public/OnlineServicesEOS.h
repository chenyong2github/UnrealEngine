// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EOS_SDK

#include "Online/OnlineServicesCommon.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_sdk.h"

using IEOSPlatformHandlePtr = TSharedPtr<class IEOSPlatformHandle>;

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
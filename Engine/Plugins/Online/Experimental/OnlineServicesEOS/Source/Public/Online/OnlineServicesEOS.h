// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineServicesCommon.h"
#include "SocketSubsystemEOS.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_sdk.h"

using IEOSPlatformHandlePtr = TSharedPtr<class IEOSPlatformHandle>;

namespace UE::Online {

class FOnlineServicesEOS;

class FSocketSubsystemEOSUtils_OnlineServicesEOS : public ISocketSubsystemEOSUtils
{
public:
	FSocketSubsystemEOSUtils_OnlineServicesEOS(FOnlineServicesEOS* InServicesEOS);
	virtual ~FSocketSubsystemEOSUtils_OnlineServicesEOS() override;

	virtual EOS_ProductUserId GetLocalUserId() override;
	virtual FString GetSessionId() override;

private:
	FOnlineServicesEOS* ServicesEOS;
};

class ONLINESERVICESEOS_API FOnlineServicesEOS : public FOnlineServicesCommon
{
public:
	using Super = FOnlineServicesCommon;

	FOnlineServicesEOS();
	virtual void RegisterComponents() override;
	virtual void Initialize() override;
	virtual TOnlineResult<FGetResolvedConnectString> GetResolvedConnectString(FGetResolvedConnectString::Params&& Params) override;

	static EOnlineServices GetServicesProvider() { return EOnlineServices::Epic; }

	EOS_HPlatform GetEOSPlatformHandle() const;
protected:
	IEOSPlatformHandlePtr EOSPlatformHandle;

	TSharedPtr<FSocketSubsystemEOS, ESPMode::ThreadSafe> SocketSubsystem;
};

/* UE::Online */ }

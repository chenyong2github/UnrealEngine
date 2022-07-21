// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EOS_SDK

#include "Features/IModularFeatures.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_init.h"
#include "eos_types.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FEOSSDKManagerOnPreInitializeSDK, EOS_InitializeOptions& Options);
DECLARE_MULTICAST_DELEGATE_OneParam(FEOSSDKManagerOnPreCreatePlatform, EOS_Platform_Options& Options);

class IEOSPlatformHandle
{
public:
	IEOSPlatformHandle(EOS_HPlatform InPlatformHandle) : PlatformHandle(InPlatformHandle) {}
	virtual ~IEOSPlatformHandle() = default;

	virtual void Tick() = 0;

	operator EOS_HPlatform() const { return PlatformHandle; }

	virtual FString GetOverrideCountryCode() const = 0;
	virtual FString GetOverrideLocaleCode() const = 0;

	virtual void LogInfo(int32 Indent = 0) const = 0;
	virtual void LogAuthInfo(const EOS_EpicAccountId LoggedInAccount, int32 Indent = 0) const = 0;
	virtual void LogUserInfo(const EOS_EpicAccountId LoggedInAccount, const EOS_EpicAccountId TargetAccount, int32 Indent = 0) const = 0;
	virtual void LogPresenceInfo(const EOS_EpicAccountId LoggedInAccount, const EOS_EpicAccountId TargetAccount, int32 Indent = 0) const = 0;
	virtual void LogFriendsInfo(const EOS_EpicAccountId LoggedInAccount, int32 Indent = 0) const = 0;
	virtual void LogConnectInfo(const EOS_ProductUserId LoggedInAccount, int32 Indent = 0) const = 0;

protected:
	EOS_HPlatform PlatformHandle;
};
using IEOSPlatformHandlePtr = TSharedPtr<IEOSPlatformHandle, ESPMode::ThreadSafe>;

class IEOSSDKManager : public IModularFeature
{
public:
	static IEOSSDKManager* Get()
	{
		if (IModularFeatures::Get().IsModularFeatureAvailable(GetModularFeatureName()))
		{
			return &IModularFeatures::Get().GetModularFeature<IEOSSDKManager>(GetModularFeatureName());
		}
		return nullptr;
	}

	static FName GetModularFeatureName()
	{
		static const FName FeatureName = TEXT("EOSSDKManager");
		return FeatureName;
	}

	virtual ~IEOSSDKManager() = default;

	virtual EOS_EResult Initialize() = 0;
	virtual bool IsInitialized() const = 0;

	virtual IEOSPlatformHandlePtr CreatePlatform(EOS_Platform_Options& PlatformOptions) = 0;

	virtual FString GetProductName() const = 0;
	virtual FString GetProductVersion() const = 0;
	virtual FString GetCacheDirBase() const = 0;
	virtual FString GetOverrideCountryCode(const EOS_HPlatform Platform) const = 0;
	virtual FString GetOverrideLocaleCode(const EOS_HPlatform Platform) const = 0;

	virtual void LogInfo(int32 Indent = 0) const = 0;
	virtual void LogPlatformInfo(const EOS_HPlatform Platform, int32 Indent = 0) const = 0;
	virtual void LogAuthInfo(const EOS_HPlatform Platform, const EOS_EpicAccountId LoggedInAccount, int32 Indent = 0) const = 0;
	virtual void LogUserInfo(const EOS_HPlatform Platform, const EOS_EpicAccountId LoggedInAccount, const EOS_EpicAccountId TargetAccount, int32 Indent = 0) const = 0;
	virtual void LogPresenceInfo(const EOS_HPlatform Platform, const EOS_EpicAccountId LoggedInAccount, const EOS_EpicAccountId TargetAccount, int32 Indent = 0) const = 0;
	virtual void LogFriendsInfo(const EOS_HPlatform Platform, const EOS_EpicAccountId LoggedInAccount, int32 Indent = 0) const = 0;
	virtual void LogConnectInfo(const EOS_HPlatform Platform, const EOS_ProductUserId LoggedInAccount, int32 Indent = 0) const = 0;

	FEOSSDKManagerOnPreInitializeSDK OnPreInitializeSDK;
	FEOSSDKManagerOnPreCreatePlatform OnPreCreatePlatform;
};

#endif // WITH_EOS_SDK
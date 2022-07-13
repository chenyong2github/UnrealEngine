// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineServicesCommon.h"

class IOnlineSubsystem;

namespace UE::Online {

class FOnlineUniqueNetIdRegistry;

class FOnlineServicesOSSAdapter : public FOnlineServicesCommon
{
public:
	using Super = FOnlineServicesCommon;

	ONLINESERVICESOSSADAPTER_API FOnlineServicesOSSAdapter(EOnlineServices InServicesType, const FString& InConfigName, FName InInstanceName, IOnlineSubsystem* InSubsystem);

	ONLINESERVICESOSSADAPTER_API virtual void RegisterComponents() override;
	ONLINESERVICESOSSADAPTER_API virtual void Initialize() override;
	virtual EOnlineServices GetServicesProvider() const override { return ServicesType; }

	IOnlineSubsystem& GetSubsystem() const { return *Subsystem; }
	FOnlineUniqueNetIdRegistry& GetAccountIdRegistry() const { return *AccountIdRegistry; }
	FOnlineUniqueNetIdRegistry& GetAccountIdRegistry() { return *AccountIdRegistry; }

protected:
	EOnlineServices ServicesType;
	IOnlineSubsystem* Subsystem;
	FOnlineUniqueNetIdRegistry* AccountIdRegistry;
};

/* UE::Online */ }

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServicesOSSAdapter.h"

#include "Online/OnlineIdOSSAdapter.h"
#include "Online/AuthOSSAdapter.h"

#include "OnlineSubsystem.h"

namespace UE::Online {

FOnlineServicesOSSAdapter::FOnlineServicesOSSAdapter(EOnlineServices InServicesType, const FString& InConfigName, IOnlineSubsystem* InSubsystem)
	: FOnlineServicesCommon(InConfigName)
	, ServicesType(InServicesType)
	, Subsystem(InSubsystem)
{
}

void FOnlineServicesOSSAdapter::RegisterComponents()
{
	Components.Register<FAuthOSSAdapter>(*this);

	FOnlineServicesCommon::RegisterComponents();
}

void FOnlineServicesOSSAdapter::Initialize()
{
	AccountIdRegistry = static_cast<FOnlineUniqueNetIdRegistry*>(FOnlineIdRegistryRegistry::Get().GetAccountIdRegistry(ServicesType));

	FOnlineServicesCommon::Initialize();
}

/* UE::Online*/ }

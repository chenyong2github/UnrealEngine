// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServicesOSSAdapter.h"

#include "Online/OnlineIdOSSAdapter.h"
#include "Online/AuthOSSAdapter.h"
#include "Online/ConnectivityOSSAdapter.h"
#include "Online/PrivilegesOSSAdapter.h"
#include "Online/ExternalUIOSSAdapter.h"

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
	Components.Register<FConnectivityOSSAdapter>(*this);
	Components.Register<FPrivilegesOSSAdapter>(*this);

	if (Subsystem->GetExternalUIInterface().IsValid())
	{
		Components.Register<FExternalUIOSSAdapter>(*this);
	}

	FOnlineServicesCommon::RegisterComponents();
}

void FOnlineServicesOSSAdapter::Initialize()
{
	AccountIdRegistry = static_cast<FOnlineUniqueNetIdRegistry*>(FOnlineIdRegistryRegistry::Get().GetAccountIdRegistry(ServicesType));

	FOnlineServicesCommon::Initialize();
}

/* UE::Online*/ }

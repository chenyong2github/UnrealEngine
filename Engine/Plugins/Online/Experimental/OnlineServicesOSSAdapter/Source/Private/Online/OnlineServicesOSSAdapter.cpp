// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServicesOSSAdapter.h"

#include "Online/AchievementsOSSAdapter.h"
#include "Online/AuthOSSAdapter.h"
#include "Online/ConnectivityOSSAdapter.h"
#include "Online/ExternalUIOSSAdapter.h"
#include "Online/OnlineIdOSSAdapter.h"
#include "Online/PresenceOSSAdapter.h"
#include "Online/PrivilegesOSSAdapter.h"
#include "Online/StatsOSSAdapter.h"

#include "OnlineSubsystem.h"

namespace UE::Online {

FOnlineServicesOSSAdapter::FOnlineServicesOSSAdapter(EOnlineServices InServicesType, const FString& InConfigName, FName InInstanceName, IOnlineSubsystem* InSubsystem)
	: FOnlineServicesCommon(InConfigName, InInstanceName)
	, ServicesType(InServicesType)
	, Subsystem(InSubsystem)
{
}

void FOnlineServicesOSSAdapter::RegisterComponents()
{
	Components.Register<FAuthOSSAdapter>(*this);
	Components.Register<FConnectivityOSSAdapter>(*this);
	Components.Register<FPresenceOSSAdapter>(*this);
	Components.Register<FPrivilegesOSSAdapter>(*this);
	Components.Register<FStatsOSSAdapter>(*this);

	if (Subsystem->GetAchievementsInterface().IsValid())
	{
		Components.Register<FAchievementsOSSAdapter>(*this);
	}
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

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServicesCommon.h"

#include "Online/AuthCommon.h"
#include "Online/FriendsCommon.h"

namespace UE::Online {

FOnlineServicesCommon::FOnlineServicesCommon()
	: OpCache(TEXT("OnlineServicesCommon"), *this)
	, ConfigProvider(MakeUnique<FOnlineConfigProviderGConfig>(GEngineIni))
{
}

void FOnlineServicesCommon::Init()
{
	RegisterComponents();
	Initialize();
	PostInitialize();
}

void FOnlineServicesCommon::Destroy()
{
	PreShutdown();
	Shutdown();
}

IAuthPtr FOnlineServicesCommon::GetAuthInterface()
{
	return IAuthPtr(AsShared(), Get<IAuth>());
}

IFriendsPtr FOnlineServicesCommon::GetFriendsInterface()
{
	return IFriendsPtr(AsShared(), Get<IFriends>());
}

void FOnlineServicesCommon::RegisterComponents()
{
}

void FOnlineServicesCommon::Initialize()
{
	Components.Visit(&IOnlineComponent::Initialize);
}

void FOnlineServicesCommon::PostInitialize()
{
	Components.Visit(&IOnlineComponent::PostInitialize);
}

void FOnlineServicesCommon::LoadConfig()
{
	Components.Visit(&IOnlineComponent::LoadConfig);
}

bool FOnlineServicesCommon::Tick(float DeltaSeconds)
{
	Components.Visit(&IOnlineComponent::Tick, DeltaSeconds);

	return true;
}

void FOnlineServicesCommon::PreShutdown()
{
	Components.Visit(&IOnlineComponent::PreShutdown);
}

void FOnlineServicesCommon::Shutdown()
{
	Components.Visit(&IOnlineComponent::Shutdown);
}

/* UE::Online */ }

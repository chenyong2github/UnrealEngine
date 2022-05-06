// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServicesEOS.h"

#include "Online/AuthEOS.h"
#include "Online/FriendsEOS.h"
#include "Online/PresenceEOS.h"
#include "Online/ExternalUIEOS.h"

namespace UE::Online {

FOnlineServicesEOS::FOnlineServicesEOS(FName InInstanceName)
	: Super(InInstanceName)
{
}

void FOnlineServicesEOS::RegisterComponents()
{
	Components.Register<FAuthEOS>(*this);
	Components.Register<FFriendsEOS>(*this);
	Components.Register<FPresenceEOS>(*this);
	Components.Register<FExternalUIEOS>(*this);

	Super::RegisterComponents();
}

/* UE::Online */ }

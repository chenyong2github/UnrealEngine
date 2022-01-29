// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServices.h"

#include "Online/OnlineServicesRegistry.h"

namespace UE::Online {

bool IsLoaded(EOnlineServices OnlineServices, FName InstanceName)
{
	return FOnlineServicesRegistry::Get().IsLoaded(OnlineServices, InstanceName);
}

TSharedPtr<IOnlineServices> GetServices(EOnlineServices OnlineServices, FName InstanceName)
{
	return FOnlineServicesRegistry::Get().GetNamedServicesInstance(OnlineServices, InstanceName);
}

void DestroyServices(EOnlineServices OnlineServices, FName InstanceName)
{
	FOnlineServicesRegistry::Get().DestroyNamedServicesInstance(OnlineServices, InstanceName);
}

/* UE::Online */ }

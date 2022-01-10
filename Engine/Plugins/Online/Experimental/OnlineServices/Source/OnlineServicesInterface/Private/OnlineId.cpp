// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineId.h"

#include "Misc/LazySingleton.h"

namespace UE::Online {

FOnlineIdRegistryRegistry& FOnlineIdRegistryRegistry::Get()
{
	return TLazySingleton<FOnlineIdRegistryRegistry>::Get();
}

void FOnlineIdRegistryRegistry::TearDown()
{
	return TLazySingleton<FOnlineIdRegistryRegistry>::TearDown();
}

void FOnlineIdRegistryRegistry::RegisterAccountIdRegistry(EOnlineServices OnlineServices, IOnlineAccountIdRegistry* Registry, int32 Priority)
{
	FAccountIdRegistryAndPriority* Found = AccountIdRegistries.Find(OnlineServices);
	if (Found == nullptr || Found->Priority < Priority)
	{
		AccountIdRegistries.Emplace(OnlineServices, FAccountIdRegistryAndPriority(Registry, Priority));
	}
}

void FOnlineIdRegistryRegistry::UnregisterAccountIdRegistry(EOnlineServices OnlineServices, int32 Priority)
{
	FAccountIdRegistryAndPriority* Found = AccountIdRegistries.Find(OnlineServices);
	if (Found != nullptr && Found->Priority == Priority)
	{
		AccountIdRegistries.Remove(OnlineServices);
	}
}

IOnlineAccountIdRegistry* FOnlineIdRegistryRegistry::GetAccountIdRegistry(EOnlineServices OnlineServices)
{
	IOnlineAccountIdRegistry* Registry = nullptr;
	if (FAccountIdRegistryAndPriority* Found = AccountIdRegistries.Find(OnlineServices))
	{
		Registry = Found->Registry;
	}
	return Registry;
}

FString ToLogString(const FOnlineAccountIdHandle& Id)
{
	FString Result;
	if (IOnlineAccountIdRegistry* Registry = FOnlineIdRegistryRegistry::Get().GetAccountIdRegistry(Id.GetOnlineServicesType()))
	{
		Result = Registry->ToLogString(Id);
	}
	return Result;
}


/* UE::Online */ }

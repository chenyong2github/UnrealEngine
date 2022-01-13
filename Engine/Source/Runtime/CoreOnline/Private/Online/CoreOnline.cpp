// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/CoreOnline.h"

#include "Misc/LazySingleton.h"
#include "Online/CoreOnlinePrivate.h"

namespace UE::Online {

FOnlineIdRegistryRegistry& FOnlineIdRegistryRegistry::Get()
{
	return TLazySingleton<FOnlineIdRegistryRegistry>::Get();
}

FOnlineIdRegistryRegistry::FOnlineIdRegistryRegistry()
	: ForeignAccountIdRegistry(MakeUnique<FOnlineForeignAccountIdRegistry>())
{
}

FOnlineIdRegistryRegistry::~FOnlineIdRegistryRegistry()
{
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

FString FOnlineIdRegistryRegistry::ToLogString(const FOnlineAccountIdHandle& Handle) const
{
	FString Result;
	if (IOnlineAccountIdRegistry* Registry = GetAccountIdRegistry(Handle.GetOnlineServicesType()))
	{
		Result = Registry->ToLogString(Handle);
	}
	else
	{
		Result = ForeignAccountIdRegistry->ToLogString(Handle);
	}
	return Result;
}

TArray<uint8> FOnlineIdRegistryRegistry::ToReplicationData(const FOnlineAccountIdHandle& Handle) const
{
	TArray<uint8> RepData;
	if (IOnlineAccountIdRegistry* Registry = GetAccountIdRegistry(Handle.GetOnlineServicesType()))
	{
		RepData = Registry->ToReplicationData(Handle);
	}
	else
	{
		RepData = ForeignAccountIdRegistry->ToReplicationData(Handle);
	}
	return RepData;
}

FOnlineAccountIdHandle FOnlineIdRegistryRegistry::ToAccountId(EOnlineServices Services, const TArray<uint8>& RepData) const
{
	FOnlineAccountIdHandle Handle;
	if (IOnlineAccountIdRegistry* Registry = GetAccountIdRegistry(Handle.GetOnlineServicesType()))
	{
		Handle = Registry->FromReplicationData(RepData);
	}
	else
	{
		Handle = ForeignAccountIdRegistry->FromReplicationData(Services, RepData);
	}
	return Handle;
}

IOnlineAccountIdRegistry* FOnlineIdRegistryRegistry::GetAccountIdRegistry(EOnlineServices OnlineServices) const
{
	IOnlineAccountIdRegistry* Registry = nullptr;
	if (const FAccountIdRegistryAndPriority* Found = AccountIdRegistries.Find(OnlineServices))
	{
		Registry = Found->Registry;
	}
	return Registry;
}

FString ToLogString(const FOnlineAccountIdHandle& Id)
{
	return FOnlineIdRegistryRegistry::Get().ToLogString(Id);
}

}	/* UE::Online */

FString FUniqueNetIdWrapper::ToDebugString() const
{
	FString Result;
	if (IsValid())
	{
		if (IsV1())
		{
			const FUniqueNetIdPtr& Ptr = GetV1();
			Result = FString::Printf(TEXT("%s:%s"), *Ptr->GetType().ToString(), *Ptr->ToDebugString());
		}
		else
		{
			Result = ToLogString(GetV2());
		}
	}
	else
	{
		Result = TEXT("INVALID");
	}
	return Result;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServicesRegistry.h"

#include "Misc/LazySingleton.h"

namespace UE::Online {

FOnlineServicesRegistry& FOnlineServicesRegistry::Get()
{
	return TLazySingleton<FOnlineServicesRegistry>::Get();
}

void FOnlineServicesRegistry::TearDown()
{
	return TLazySingleton<FOnlineServicesRegistry>::TearDown();
}

void FOnlineServicesRegistry::RegisterServicesFactory(EOnlineServices OnlineServices, TUniquePtr<IOnlineServicesFactory>&& Factory, int32 Priority)
{
	FFactoryAndPriority* ExistingFactoryAndPriority = ServicesFactories.Find(OnlineServices);
	if (ExistingFactoryAndPriority == nullptr || ExistingFactoryAndPriority->Priority < Priority)
	{
		ServicesFactories.Add(OnlineServices, FFactoryAndPriority(MoveTemp(Factory), Priority));
	}
}

void FOnlineServicesRegistry::UnRegisterServicesFactory(EOnlineServices OnlineServices, int32 Priority)
{
	FFactoryAndPriority* ExistingFactoryAndPriority = ServicesFactories.Find(OnlineServices);
	if (ExistingFactoryAndPriority != nullptr && ExistingFactoryAndPriority->Priority == Priority)
	{
		ServicesFactories.Remove(OnlineServices);
	}
}

TSharedPtr<IOnlineServices> FOnlineServicesRegistry::GetNamedServicesInstance(EOnlineServices OnlineServices, FName InstanceName)
{
	TSharedPtr<IOnlineServices> Services;

	if (TSharedPtr<IOnlineServices>* ServicesPtr = NamedServiceInstances.FindOrAdd(OnlineServices).Find(InstanceName))
	{
		Services = *ServicesPtr;
	}
	else
	{
		Services = CreateServices(OnlineServices);
		if (Services.IsValid())
		{
			NamedServiceInstances.FindOrAdd(OnlineServices).Add(InstanceName, Services);
		}
	}

	return Services;
}

void FOnlineServicesRegistry::DestroyNamedServicesInstance(EOnlineServices OnlineServices, FName InstanceName)
{
	if (TSharedPtr<IOnlineServices>* ServicesPtr = NamedServiceInstances.FindOrAdd(OnlineServices).Find(InstanceName))
	{
		(*ServicesPtr)->Destroy();

		NamedServiceInstances.FindOrAdd(OnlineServices).Remove(InstanceName);
	}
}


TSharedPtr<IOnlineServices> FOnlineServicesRegistry::CreateServices(EOnlineServices OnlineServices)
{
	TSharedPtr<IOnlineServices> Services;

	FFactoryAndPriority* FactoryAndPriority = ServicesFactories.Find(OnlineServices);
	if (FactoryAndPriority != nullptr)
	{
		Services = FactoryAndPriority->Factory->Create();
		Services->Init();
	}

	return Services;
}

/* UE::Online */ }

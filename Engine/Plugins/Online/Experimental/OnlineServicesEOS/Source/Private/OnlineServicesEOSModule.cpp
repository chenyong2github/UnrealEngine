// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "Online/OnlineServicesRegistry.h"
#include "Online/OnlineServicesEOS.h"
#include "Online/OnlineIdEOS.h"

namespace UE::Online
{

class FOnlineServicesEOSModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
protected:
};

class FOnlineServicesFactoryEOS : public IOnlineServicesFactory
{
public:
	virtual ~FOnlineServicesFactoryEOS() {}
	virtual TSharedPtr<IOnlineServices> Create(FName InInstanceName) override
	{
		return MakeShared<FOnlineServicesEOS>(InInstanceName);
	}
protected:
};

void FOnlineServicesEOSModule::StartupModule()
{
	const FName EOSSharedModuleName = TEXT("EOSShared");
	if (!FModuleManager::Get().IsModuleLoaded(EOSSharedModuleName))
	{
		FModuleManager::Get().LoadModuleChecked(EOSSharedModuleName);
	}

	// Make this higher priority that EOSGS
	const int Priority = 1;
	FOnlineServicesRegistry::Get().RegisterServicesFactory(EOnlineServices::Epic, MakeUnique<FOnlineServicesFactoryEOS>(), Priority);
	FOnlineIdRegistryRegistry::Get().RegisterAccountIdRegistry(EOnlineServices::Epic, &FOnlineAccountIdRegistryEOS::Get(), Priority);
}

/* UE::Online */ }

IMPLEMENT_MODULE(UE::Online::FOnlineServicesEOSModule, OnlineServicesEOS);

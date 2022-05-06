// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Online/OnlineIdEOSGS.h"
#include "Online/OnlineServicesEOSGS.h"
#include "Online/OnlineServicesRegistry.h"

#include "CoreMinimal.h"

namespace UE::Online
{

class FOnlineServicesEOSGSModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
protected:
};

class FOnlineServicesFactoryEOSGS : public IOnlineServicesFactory
{
public:
	virtual ~FOnlineServicesFactoryEOSGS() {}
	virtual TSharedPtr<IOnlineServices> Create(FName InInstanceName) override
	{
		return MakeShared<FOnlineServicesEOSGS>(InInstanceName);
	}
protected:
};

void FOnlineServicesEOSGSModule::StartupModule()
{
	FOnlineServicesRegistry::Get().RegisterServicesFactory(EOnlineServices::Epic, MakeUnique<FOnlineServicesFactoryEOSGS>());
	FOnlineIdRegistryRegistry::Get().RegisterAccountIdRegistry(EOnlineServices::Epic, &FOnlineAccountIdRegistryEOSGS::Get());
}

/* UE::Online */ }

IMPLEMENT_MODULE(UE::Online::FOnlineServicesEOSGSModule, OnlineServicesEOSGS);

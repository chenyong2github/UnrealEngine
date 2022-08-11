// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Online/OnlineIdEOSGS.h"
#include "Online/OnlineServicesEOSGS.h"
#include "Online/OnlineServicesRegistry.h"
#include "Online/OnlineServicesEOSGSPlatformFactory.h"
#include "Online/SessionsEOSGS.h"

#include "CoreMinimal.h"

namespace UE::Online
{

class FOnlineServicesEOSGSModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
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
	FOnlineIdRegistryRegistry::Get().RegisterSessionIdRegistry(EOnlineServices::Epic, &FOnlineSessionIdRegistryEOSGS::Get());
	FOnlineIdRegistryRegistry::Get().RegisterSessionInviteIdRegistry(EOnlineServices::Epic, &FOnlineSessionInviteIdRegistryEOSGS::Get());

	// Initialize the platform factory on startup.  This is necessary for the SDK to bind to rendering and input very early.
	FOnlineServicesEOSGSPlatformFactory::Get();
}

void FOnlineServicesEOSGSModule::ShutdownModule()
{
	UE::Online::FOnlineServicesEOSGSPlatformFactory::TearDown();
}

/* UE::Online */ }

IMPLEMENT_MODULE(UE::Online::FOnlineServicesEOSGSModule, OnlineServicesEOSGS);

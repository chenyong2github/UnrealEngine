// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "Online/OnlineServicesRegistry.h"
#include "Online/OnlineServicesNull.h"
#include "Online/AuthNull.h"

namespace UE::Online
{

class FOnlineServicesNullModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
protected:
};

class FOnlineServicesFactoryNull : public IOnlineServicesFactory
{
public:
	virtual ~FOnlineServicesFactoryNull() {}
	virtual TSharedPtr<IOnlineServices> Create() override
	{
		return MakeShared<FOnlineServicesNull>();
	}
protected:
};

void FOnlineServicesNullModule::StartupModule()
{
	FOnlineServicesRegistry::Get().RegisterServicesFactory(EOnlineServices::Null, MakeUnique<FOnlineServicesFactoryNull>());
	FOnlineIdRegistryRegistry::Get().RegisterAccountIdRegistry(EOnlineServices::Null, &FOnlineAccountIdRegistryNull::Get());
}

/* UE::Online */ }

IMPLEMENT_MODULE(UE::Online::FOnlineServicesNullModule, OnlineServicesNull);

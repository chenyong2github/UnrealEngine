// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "Online/OnlineServicesRegistry.h"
#include "OnlineServicesEOS.h"

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
	virtual TSharedPtr<IOnlineServices> Create() override
	{
		return MakeShared<FOnlineServicesEOS>();
	}
protected:
};

void FOnlineServicesEOSModule::StartupModule()
{
	FOnlineServicesRegistry::Get().RegisterServicesFactory(EOnlineServices::Epic, MakeUnique<FOnlineServicesFactoryEOS>());
}

/* UE::Online */ }

IMPLEMENT_MODULE(UE::Online::FOnlineServicesEOSModule, OnlineServicesEOS);

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

#if WITH_EOS_SDK
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
#endif // WITH_EOS_SDK

void FOnlineServicesEOSModule::StartupModule()
{
#if WITH_EOS_SDK
	FOnlineServicesRegistry::Get().RegisterServicesFactory(EOnlineServices::Epic, MakeUnique<FOnlineServicesFactoryEOS>());
#endif // WITH_EOS_SDK
}

/* UE::Online */ }

IMPLEMENT_MODULE(UE::Online::FOnlineServicesEOSModule, OnlineServicesEOS);

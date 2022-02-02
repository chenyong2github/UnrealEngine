// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "Online/OnlineServicesRegistry.h"

namespace UE::Online
{

class FOnlineServicesOSSAdapterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
protected:
};

void FOnlineServicesOSSAdapterModule::StartupModule()
{
}

/* UE::Online */ }

IMPLEMENT_MODULE(UE::Online::FOnlineServicesOSSAdapterModule, OnlineServicesOSSAdapter);

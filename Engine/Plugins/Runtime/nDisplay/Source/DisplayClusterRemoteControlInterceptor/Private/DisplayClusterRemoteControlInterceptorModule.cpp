// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRemoteControlInterceptorModule.h"
#include "DisplayClusterRemoteControlInterceptorLog.h"
#include "DisplayClusterRemoteControlInterceptor.h"

#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"
#include "IRemoteControlInterceptionFeature.h"


void FDisplayClusterRemoteControlInterceptorModule::StartupModule()
{
	// Instantiate the interceptor feature on module start
	Interceptor = MakeUnique<FDisplayClusterRemoteControlInterceptor>();
	// Register the interceptor feature
	IModularFeatures::Get().RegisterModularFeature(IRemoteControlInterceptionFeatureInterceptor::GetName(), Interceptor.Get());
}

void FDisplayClusterRemoteControlInterceptorModule::ShutdownModule()
{
	// Unregister the interceptor feature on module shutdown
	IModularFeatures::Get().UnregisterModularFeature(IRemoteControlInterceptionFeatureInterceptor::GetName(), Interceptor.Get());
}

IMPLEMENT_MODULE(FDisplayClusterRemoteControlInterceptorModule, DisplayClusterRemoteControlInterceptor);

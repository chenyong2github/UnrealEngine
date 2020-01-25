// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguration.h"
#include "Modules/ModuleManager.h"

#include "Engine/Engine.h"
#include "Engine/Console.h"

#include "DisplayClusterConfigurationLog.h"
#include "DisplayClusterConfigurationStrings.h"

#include "DisplayClusterUtils/DisplayClusterCommonHelpers.h"


#define LOCTEXT_NAMESPACE "DisplayClusterConfiguration"

void FDisplayClusterConfigurationModule::StartupModule()
{
	FString ConfigLineStr = FCommandLine::Get();

	int32 GraphicsAdapter;
	if (DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::args::Gpu, GraphicsAdapter))
	{
		IConsoleVariable* const GpuCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GraphicsAdapter"));
		if (GpuCVar)
		{
			UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Set custom GPU selection policy - r.GraphicsAdapter=%d"), GraphicsAdapter);
			GpuCVar->Set(GraphicsAdapter);
		}
	}
}

void FDisplayClusterConfigurationModule::ShutdownModule()
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Module shutdown"));
}

IMPLEMENT_MODULE(FDisplayClusterConfigurationModule, DisplayClusterConfiguration);

#undef LOCTEXT_NAMESPACE

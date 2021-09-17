// Copyright Epic Games, Inc. All Rights Reserved.

#include "HordeExecutorModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ISettingsModule.h"
#include "HordeExecutorSettings.h"

IMPLEMENT_MODULE(UE::RemoteExecution::FHordeExecutorModule, HordeExecutor);

#define LOCTEXT_NAMESPACE "HordeExecutorModule"

DEFINE_LOG_CATEGORY(LogHordeExecutor);

static FName RemoteExecutionFeatureName(TEXT("RemoteExecution"));


namespace UE::RemoteExecution
{
	void FHordeExecutorModule::StartupModule()
	{
		GetMutableDefault<UHordeExecutorSettings>()->LoadConfig();

		const UHordeExecutorSettings* HordeExecutorSettings = GetDefault<UHordeExecutorSettings>();

		FHordeExecutor::FSettings Settings;
		Settings.ContentAddressableStorageTarget = HordeExecutorSettings->ContentAddressableStorageTarget;
		Settings.ExecutionTarget = HordeExecutorSettings->ExecutionTarget;
		Settings.ContentAddressableStorageHeaders = HordeExecutorSettings->ContentAddressableStorageHeaders;
		Settings.ExecutionHeaders = HordeExecutorSettings->ExecutionHeaders;

		HordeExecution.Initialize(Settings);

		IModularFeatures::Get().RegisterModularFeature(RemoteExecutionFeatureName, &HordeExecution);
	}

	void FHordeExecutorModule::ShutdownModule()
	{
		HordeExecution.Shutdown();
		IModularFeatures::Get().UnregisterModularFeature(RemoteExecutionFeatureName, &HordeExecution);
	}

	bool FHordeExecutorModule::SupportsDynamicReloading()
	{
		return true;
	}
}

#undef LOCTEXT_NAMESPACE

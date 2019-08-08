// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "InstallBundleManagerInterface.h"
#include "InstallBundleManagerModule.h"

FInstallBundleCompleteMultiDelegate IInstallBundleManager::InstallBundleCompleteDelegate;

FInstallBundleCompleteMultiDelegate IInstallBundleManager::RemoveBundleCompleteDelegate;

FInstallBundlePausedMultiDelegate IInstallBundleManager::PausedBundleDelegate;

IInstallBundleManager* IInstallBundleManager::GetPlatformInstallBundleManager()
{
	static IInstallBundleManager* Manager = nullptr;
	static bool bCheckedIni = false;

	if (Manager)
		return Manager;

	if (!bCheckedIni && !GEngineIni.IsEmpty())
	{
		FString ModuleName;
		IInstallBundleManagerModule* Module = nullptr;
		GConfig->GetString(TEXT("InstallBundleManager"), TEXT("ModuleName"), ModuleName, GEngineIni);

		if (FModuleManager::Get().ModuleExists(*ModuleName))
		{
			FModuleStatus Status;
			Module = FModuleManager::LoadModulePtr<IInstallBundleManagerModule>(*ModuleName);
			if (Module)
			{
				Manager = Module->GetInstallBundleManager();
			}
		}

		bCheckedIni = true;
	}

	return Manager;
}


// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "InstallBundleManagerInterface.h"

/**
 * Currently empty implementation for InstallBundleModule until things are moved in here.
 */
class FInstallBundleManagerModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

/**
 * Base Module Interface for InstallBundleManager implementation modules
 */
class IInstallBundleManagerModule : public IModuleInterface
{
public:
	virtual void PreUnloadCallback() override
	{
		InstallBundleManager = nullptr;
	}

	TSharedPtr<IInstallBundleManager> GetInstallBundleManager()
	{
		return InstallBundleManager;
	}

protected:
	TSharedPtr<IInstallBundleManager> InstallBundleManager;
	TWeakPtr<IInstallBundleManager> LastInstallBundleManager;
};

/**
 * Module Interface for InstallBundleManager implementation modules
 */
template<class InstallBundleManagerModuleImpl>
class TInstallBundleManagerModule : public IInstallBundleManagerModule
{
public:
	virtual void StartupModule() override
	{
		// Only instantiate the bundle manager if this is the version the game has been configured to use
		FString ModuleName;
#if WITH_EDITOR
		GConfig->GetString(TEXT("InstallBundleManager"), TEXT("EditorModuleName"), ModuleName, GEngineIni);
#else
		GConfig->GetString(TEXT("InstallBundleManager"), TEXT("ModuleName"), ModuleName, GEngineIni);
#endif // WITH_EDITOR

		if (FModuleManager::Get().GetModule(*ModuleName) == this)
		{
			check(LastInstallBundleManager == nullptr);
			InstallBundleManager = MakeShared<InstallBundleManagerModuleImpl>();
			LastInstallBundleManager = InstallBundleManager;
		}
	}
};

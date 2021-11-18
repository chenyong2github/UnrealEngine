// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IDerivedDataCacheNotifications.h"

class SDockTab;
class SWindow;
class SWidget;
class SDerivedDataCacheSettingsDialog;
class FSpawnTabArgs;

/**
 * The module holding all of the UI related pieces for DerivedData
 */
class DERIVEDDATAEDITOR_API FDerivedDataEditorModule : public IModuleInterface
{
public:

	/**
	 * Called right after the module DLL has been loaded and the module object has been created
	 */
	virtual void StartupModule() override;

	/**
	 * Called before the module is unloaded, right before the module object is destroyed.
	 */
	virtual void ShutdownModule() override;

	TSharedRef<SWidget>	CreateStatusBarWidget();
	IDerivedDataCacheNotifications& GetCacheNotifcations() { return *DerivedDataCacheNotifications; }

	void ShowResourceUsageTab();
	void ShowCacheStatisticsTab();

	void ShowVirtualAssetsStatisticsTab();
	void ShowSettingsDialog();

private:

	TSharedPtr<SWidget> CreateResourceUsageDialog();
	TSharedPtr<SWidget> CreateCacheStatisticsDialog();
	TSharedPtr<SWidget> CreateVirtualAssetsStatisticsDialog();


	TSharedRef<SDockTab> CreateResourceUsageTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> CreateCacheStatisticsTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> CreateVirtualAssetsStatisticsTab(const FSpawnTabArgs& Args);

	void OnSettingsDialogClosed(const TSharedRef<SWindow>& InWindow);

	TWeakPtr<SDockTab> ResourceUsageTab;
	TWeakPtr<SDockTab> CacheStatisticsTab;
	TWeakPtr<SDockTab> VirtualAssetsStatisticsTab;

	TSharedPtr<SWindow>	SettingsWindow;
	TSharedPtr<SDerivedDataCacheSettingsDialog> SettingsDialog;
	TUniquePtr<IDerivedDataCacheNotifications>	DerivedDataCacheNotifications;
};



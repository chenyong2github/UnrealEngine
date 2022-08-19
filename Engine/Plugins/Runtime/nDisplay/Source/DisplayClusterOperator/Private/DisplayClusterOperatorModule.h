// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDisplayClusterOperator.h"

class FDisplayClusterOperatorViewModel;
class FSpawnTabArgs;
class SDockTab;
class SDisplayClusterOperatorPanel;

/**
 * Display Cluster editor module
 */
class FDisplayClusterOperatorModule :
	public IDisplayClusterOperator
{
public:
	/** The name of the tab that the operator panel lives in */
	static const FName OperatorPanelTabName;

public:
	//~ IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface

	//~ IDisplayClusterOperator interface
	virtual TSharedRef<IDisplayClusterOperatorViewModel> GetOperatorViewModel() override;
	virtual FOnRegisterLayoutExtensions& OnRegisterLayoutExtensions() override { return RegisterLayoutExtensions; }
	virtual FOnRegisterStatusBarExtensions& OnRegisterStatusBarExtensions() override { return RegisterStatusBarExtensions; }
	virtual FOnDetailObjectsChanged& OnDetailObjectsChanged() override { return DetailObjectsChanged; }

	virtual FName GetPrimaryOperatorExtensionId() override;
	virtual FName GetAuxilliaryOperatorExtensionId() override;
	virtual TSharedPtr<FExtensibilityManager> GetOperatorToolBarExtensibilityManager() override { return OperatorToolBarExtensibilityManager; }
	virtual void GetRootActorLevelInstances(TArray<ADisplayClusterRootActor*>& OutRootActorInstances) override;
	virtual void ShowDetailsForObject(UObject* Object) override;
	virtual void ShowDetailsForObjects(const TArray<UObject*>& Objects) override;
	virtual void ForceDismissDrawers() override;
	//~ End IDisplayClusterOperator interface

private:
	void RegisterTabSpawners();
	void UnregisterTabSpawners();

	TSharedRef<SDockTab> SpawnOperatorPanelTab(const FSpawnTabArgs& SpawnTabArgs);
	void OnOperatorPanelTabClosed(TSharedRef<SDockTab> Tab);

private:
	FOnRegisterLayoutExtensions RegisterLayoutExtensions;
	FOnRegisterStatusBarExtensions RegisterStatusBarExtensions;
	FOnDetailObjectsChanged DetailObjectsChanged;

	TWeakPtr<SDisplayClusterOperatorPanel> ActiveOperatorPanel;
	TSharedPtr<FDisplayClusterOperatorViewModel> OperatorViewModel;
	TSharedPtr<FExtensibilityManager> OperatorToolBarExtensibilityManager;
};

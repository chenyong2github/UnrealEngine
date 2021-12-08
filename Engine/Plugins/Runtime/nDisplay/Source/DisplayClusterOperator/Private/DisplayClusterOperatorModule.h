// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDisplayClusterOperator.h"

/**
 * Display Cluster editor module
 */
class FDisplayClusterOperatorModule :
	public IDisplayClusterOperator
{
public:
	//~ IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface

	//~ IDisplayClusterOperator interface
	virtual FOnRegisterLayoutExtensions& OnRegisterLayoutExtensions() override { return RegisterLayoutExtensions; }
	virtual FOnActiveRootActorChanged& OnActiveRootActorChanged() override { return RootActorChanged; }
	virtual FOnDetailObjectsChanged& OnDetailObjectsChanged() override { return DetailObjectsChanged; }

	virtual FName GetOperatorExtensionId() override;
	virtual TSharedPtr<FExtensibilityManager> GetOperatorToolBarExtensibilityManager() override { return OperatorToolBarExtensibilityManager; }
	virtual void GetRootActorLevelInstances(TArray<ADisplayClusterRootActor*>& OutRootActorInstances) override;
	virtual void ShowDetailsForObject(UObject* Object) override;
	virtual void ShowDetailsForObjects(const TArray<UObject*>& Objects) override;
	//~ End IDisplayClusterOperator interface

private:
	void RegisterTabSpawners();
	void UnregisterTabSpawners();


private:
	FOnRegisterLayoutExtensions RegisterLayoutExtensions;
	FOnActiveRootActorChanged RootActorChanged;
	FOnDetailObjectsChanged DetailObjectsChanged;

	TSharedPtr<FExtensibilityManager> OperatorToolBarExtensibilityManager;
};

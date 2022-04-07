// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "UObject/SoftObjectPath.h"

class ADisplayClusterRootActor;
class FMenuBuilder;
class FUICommandList;
class IAssetRegistry;
class ISettingsSection;
class SWidget;

struct FAssetData;
struct FSoftObjectPath;

class FDisplayClusterLaunchEditorModule : public IModuleInterface
{
public:
	
	static FDisplayClusterLaunchEditorModule& Get();

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface
	
	static void OpenProjectSettings();
	
	void LaunchDisplayClusterProcess();

private:

	void OnFEngineLoopInitComplete();

	void RegisterToolbarItem();
	void RemoveToolbarItem();
	void RegisterProjectSettings() const;

	TArray<TWeakObjectPtr<ADisplayClusterRootActor>> GetAllDisplayClusterConfigsInWorld();
	bool DoesCurrentWorldHaveDisplayClusterConfig();

	void SetSelectedDisplayClusterConfigActor(ADisplayClusterRootActor* SelectedActor);
	void ToggleDisplayClusterConfigActorNodeSelected(FString InNodeName);
	bool IsDisplayClusterConfigActorNodeSelected(FString InNodeName);
	void SetSelectedConsoleVariablesAsset(const FAssetData InConsoleVariablesAsset);
	
	TSharedRef<SWidget> CreateToolbarMenuEntries();
	void AddDisplayClusterLaunchConfigurations(
		IAssetRegistry* AssetRegistry, FMenuBuilder& MenuBuilder, TArray<TWeakObjectPtr<ADisplayClusterRootActor>>& DisplayClusterConfigs);
	void AddDisplayClusterLaunchNodes(class IAssetRegistry* AssetRegistry, FMenuBuilder& MenuBuilder);
	void AddConsoleVariablesEditorAssetsToToolbarMenu(IAssetRegistry* AssetRegistry, FMenuBuilder& MenuBuilder);
	void AddOptionsToToolbarMenu(FMenuBuilder& MenuBuilder);

	bool GetConnectToMultiUser() const;

	TSharedPtr<FUICommandList> Actions;

	bool bAreConfigsFoundInWorld = false;

	FSoftObjectPath SelectedDisplayClusterConfigActor;
	TSet<FString> SelectedDisplayClusterConfigActorNodes;
	FName SelectedConsoleVariablesAssetName = NAME_None;
};

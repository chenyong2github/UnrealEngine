// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtrTemplates.h"

class ADisplayClusterRootActor;
class FMenuBuilder;
class FName;
class FString;
class FText;
class FUICommandList;
class IAssetRegistry;
class ISettingsSection;
class SWidget;

struct FAssetData;
struct FProcHandle;
struct FSoftObjectPath;
struct FSlateIcon;

class FDisplayClusterLaunchEditorModule : public IModuleInterface
{
public:

	enum class ELaunchState
	{
		NotLaunched,
		Launched
	};
	
	static FDisplayClusterLaunchEditorModule& Get();

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface
	
	static void OpenProjectSettings();

	void LaunchDisplayClusterProcess();
	void TerminateActiveDisplayClusterProcesses();

private:

	void OnFEngineLoopInitComplete();

	void RegisterToolbarItem();
	FText GetToolbarButtonTooltipText();
	FSlateIcon GetToolbarButtonIcon();
	void OnClickToolbarButton();
	void RemoveToolbarItem();
	
	void RegisterProjectSettings() const;

	/** Returns a list of selected nodes as FText separated by new lines with the Primary Node marked. */
	FText GetSelectedNodesListText() const;

	TArray<TWeakObjectPtr<ADisplayClusterRootActor>> GetAllDisplayClusterConfigsInWorld();
	bool DoesCurrentWorldHaveDisplayClusterConfig();
	void ApplyDisplayClusterConfigOverrides(class UDisplayClusterConfigurationData* ConfigDataCopy);

	void SetSelectedDisplayClusterConfigActor(ADisplayClusterRootActor* SelectedActor);
	void ToggleDisplayClusterConfigActorNodeSelected(FString InNodeName);
	bool IsDisplayClusterConfigActorNodeSelected(FString InNodeName);
	void SetSelectedConsoleVariablesAsset(const FAssetData InConsoleVariablesAsset);

	void SelectFirstNode(ADisplayClusterRootActor* InConfig);
	
	TSharedRef<SWidget> CreateToolbarMenuEntries();
	void AddDisplayClusterLaunchConfigurations(
		IAssetRegistry* AssetRegistry, FMenuBuilder& MenuBuilder, TArray<TWeakObjectPtr<ADisplayClusterRootActor>>& DisplayClusterConfigs);
	void AddDisplayClusterLaunchNodes(class IAssetRegistry* AssetRegistry, FMenuBuilder& MenuBuilder);
	void AddConsoleVariablesEditorAssetsToToolbarMenu(IAssetRegistry* AssetRegistry, FMenuBuilder& MenuBuilder);
	void AddOptionsToToolbarMenu(FMenuBuilder& MenuBuilder);

	bool GetConnectToMultiUser() const;

	void RemoveTerminatedNodeProcesses();

	bool bAreConfigsFoundInWorld = false;

	FSoftObjectPath SelectedDisplayClusterConfigActor;
	TArray<FString> SelectedDisplayClusterConfigActorNodes;
	FString SelectedDisplayClusterConfigActorPrimaryNode;
	
	FName SelectedConsoleVariablesAssetName = NAME_None;

	TArray<FProcHandle> ActiveProcesses; 
};

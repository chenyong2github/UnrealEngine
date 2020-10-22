// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IDisplayClusterConfiguratorToolkit.h"

#include "TickableEditorObject.h"
#include "EditorUndoClient.h"

class IDisplayClusterConfiguratorView;
class FDisplayClusterConfiguratorViewGeneral;
class FDisplayClusterConfiguratorViewDetails;
class FDisplayClusterConfiguratorViewLog;
class FDisplayClusterConfiguratorViewOutputMapping;
class FDisplayClusterConfiguratorViewCluster;
class FDisplayClusterConfiguratorViewScene;
class FDisplayClusterConfiguratorViewInput;
class FDisplayClusterConfiguratorViewViewport;
class UDisplayClusterConfiguratorEditor;

/**
 * nDisplay editor UI (should call functions on the subsystem or UNDisplayAssetEditor)
 */
class FDisplayClusterConfiguratorToolkit
	: public IDisplayClusterConfiguratorToolkit
	, public FTickableEditorObject
	, public FEditorUndoClient
{
public:
	static const FName TabID_General;
	static const FName TabID_Details;
	static const FName TabID_Log;
	static const FName TabID_OutputMapping;
	static const FName TabID_Scene;
	static const FName TabID_Cluster;
	static const FName TabID_Input;
	static const FName TabID_Viewport;

public:
	FDisplayClusterConfiguratorToolkit(UDisplayClusterConfiguratorEditor* InAssetEditor);

public:
	//~ Begin IDisplayClusterConfiguratorToolkit Interface
	virtual UDisplayClusterConfiguratorEditorData* GetEditorData() const override;
	virtual FDelegateHandle RegisterOnConfigReloaded(const FOnConfigReloadedDelegate& Delegate) override;
	virtual void UnregisterOnConfigReloaded(FDelegateHandle DelegateHandle) override;
	virtual FDelegateHandle RegisterOnObjectSelected(const FOnObjectSelectedDelegate& Delegate) override;
	virtual void UnregisterOnObjectSelected(FDelegateHandle DelegateHandle) override;
	virtual FDelegateHandle RegisterOnInvalidateViews(const FOnInvalidateViewsDelegate& Delegate) override;
	virtual void UnregisterOnInvalidateViews(FDelegateHandle DelegateHandle) override;
	const TArray<UObject*>& GetSelectedObjects() const override;
	virtual void SelectObjects(TArray<UObject*>& InSelectedObjects) override;
	virtual void InvalidateViews() override;
	virtual void ClearViewportSelection() override;
	virtual UDisplayClusterConfigurationData* GetConfig() const override;

	virtual TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> GetViewOutputMapping() const override;

	virtual TSharedRef<IDisplayClusterConfiguratorViewTree> GetViewCluster() const override;

	virtual TSharedRef<IDisplayClusterConfiguratorViewTree> GetViewScene() const override;
	virtual TSharedRef<IDisplayClusterConfiguratorViewTree> GetViewInput() const override;
	virtual TSharedRef<IDisplayClusterConfiguratorViewViewport> GetViewViewport() const override;
	virtual TSharedRef<IDisplayClusterConfiguratorViewLog> GetViewLog() const override;

	virtual TSharedRef<IDisplayClusterConfiguratorViewDetails> GetViewDetails() const override;
	virtual TSharedRef<IDisplayClusterConfiguratorView> GetViewGeneral() const override;

	//~ End IDisplayClusterConfiguratorToolkit Interface

protected:
	virtual TSharedPtr<FTabManager::FLayout> BuildDefaultLayout(const FString& LayoutName);

	//~ Begin IAssetEditorInstance Interface
	virtual FName GetEditorName() const override { return TEXT("DisplayClusterConfigurator"); }
	//~ End IAssetEditorInstance Interface

	//~ Begin FAssetEditorToolkit Interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void SaveAsset_Execute() override;
	//~ End FAssetEditorToolkit Interface

	//~ Begin FTickableEditorObject interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual TStatId GetStatId() const override;
	//~ End FTickableEditorObject Interface

	//~ Begin FBaseAssetToolkit interface
	virtual void RegisterToolbar() override;
	virtual void CreateWidgets() override;
	virtual void SetEditingObject(class UObject* InObject) override {};
	//~ End FBaseAssetToolkit Interface

private:
	TSharedRef<SDockTab> SpawnTab_General(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Log(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_OutputMapping(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Scene(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Cluster(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Input(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);

	//~ Begin UI command handlers
	void ImportConfig_Clicked();

	void SaveToFile_Clicked();

	void EditConfig_Clicked();
	//~ End UI command handlers

	void OnReadOnlyChanged(bool bReadOnly);

	void BindCommands();

private:
	UDisplayClusterConfiguratorEditor* Editor;

	TSharedPtr<FDisplayClusterConfiguratorViewGeneral> ViewGeneral;
	TSharedPtr<FDisplayClusterConfiguratorViewDetails> ViewDetails;
	TSharedPtr<FDisplayClusterConfiguratorViewLog> ViewLog;
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping;
	TSharedPtr<FDisplayClusterConfiguratorViewCluster> ViewCluster;
	TSharedPtr<FDisplayClusterConfiguratorViewScene> ViewScene;
	TSharedPtr<FDisplayClusterConfiguratorViewInput> ViewInput;
	TSharedPtr<FDisplayClusterConfiguratorViewViewport> ViewViewport;

	FOnConfigReloaded OnConfigReloaded;

	/** Delegate for when an item is selected */
	FOnObjectSelected OnObjectSelected;

	/** View invalidation delegate */
	FOnInvalidateViews OnInvalidateViews;

	FOnClearViewportSelection OnClearViewportSelection;

	TArray<UObject*> SelectedObjects;
};

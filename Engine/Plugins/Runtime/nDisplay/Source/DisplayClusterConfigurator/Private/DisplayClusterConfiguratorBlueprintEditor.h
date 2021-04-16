// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintEditorModes.h"
#include "Interfaces/IDisplayClusterConfiguratorBlueprintEditor.h"

class UDisplayClusterBlueprint;
class FEditorViewportTabContent;
class UDisplayClusterConfigurationData;
class IDisplayClusterConfiguratorView;
class IDisplayClusterConfiguratorViewTree;
class FDisplayClusterConfiguratorViewGeneral;
class FDisplayClusterConfiguratorViewDetails;
class IDisplayClusterConfiguratorViewOutputMapping;
class FDisplayClusterConfiguratorViewOutputMapping;
class FDisplayClusterConfiguratorViewCluster;
class FDisplayClusterConfiguratorViewScene;
class FDisplayClusterConfiguratorViewInput;
class FDisplayClusterConfiguratorToolbar;
class SDisplayClusterConfiguratorSCSEditorViewport;

/**
 * nDisplay editor UI (should call functions on the subsystem or UNDisplayAssetEditor)
 */
class FDisplayClusterConfiguratorBlueprintEditor
	: public IDisplayClusterConfiguratorBlueprintEditor
{

public:
	FDisplayClusterConfiguratorBlueprintEditor() :
		TicksForPreviewRenderCapture(0),
		bSCSEditorSelecting(false),
		bSelectSilently(false)
	{
	}

	~FDisplayClusterConfiguratorBlueprintEditor();

public:
	
	virtual void InitDisplayClusterBlueprintEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UDisplayClusterBlueprint* Blueprint);

	//~ Begin IDisplayClusterConfiguratorBlueprintEditor Interface
	virtual const TArray<UObject*>& GetSelectedObjects() const override;
	virtual bool IsObjectSelected(UObject* Obj) const override;
	//~ End IDisplayClusterConfiguratorBlueprintEditor Interface

	virtual void SelectObjects(TArray<UObject*>& InSelectedObjects, bool bFullRefresh = true);
	virtual void SelectAncillaryComponents(const TArray<FString>& ComponentNames);
	virtual void SelectAncillaryViewports(const TArray<FString>& ComponentNames);

	virtual UDisplayClusterConfigurationData* GetEditorData() const;
	virtual FDelegateHandle RegisterOnConfigReloaded(const FOnConfigReloadedDelegate& Delegate);
	virtual void UnregisterOnConfigReloaded(FDelegateHandle DelegateHandle);
	virtual FDelegateHandle RegisterOnObjectSelected(const FOnObjectSelectedDelegate& Delegate);
	virtual void UnregisterOnObjectSelected(FDelegateHandle DelegateHandle);
	virtual FDelegateHandle RegisterOnInvalidateViews(const FOnInvalidateViewsDelegate& Delegate);
	virtual void UnregisterOnInvalidateViews(FDelegateHandle DelegateHandle);
	virtual FDelegateHandle RegisterOnClusterChanged(const FOnClusterChangedDelegate& Delegate);
	virtual void UnregisterOnClusterChanged(FDelegateHandle DelegateHandle);
	
	virtual void InvalidateViews();
	virtual void ClusterChanged(bool bStructureChange = false);
	virtual void ClearViewportSelection();
	
	virtual UDisplayClusterConfigurationData* GetConfig() const;
	
	virtual TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> GetViewOutputMapping() const;
	virtual TSharedRef<IDisplayClusterConfiguratorViewTree> GetViewCluster() const;
	virtual TSharedRef<IDisplayClusterConfiguratorViewTree> GetViewInput() const;
	virtual TSharedRef<IDisplayClusterConfiguratorView> GetViewGeneral() const;

	TSharedPtr<SDockTab> GetViewportTab() const { return ViewportTab; }
	TSharedPtr<FEditorViewportTabContent> GetViewportTabContent() const { return ViewportTabContent; }
	
	TSharedPtr<SWidget> GetSCSEditorWrapper() const { return SCSEditorWrapper; }

	TSharedPtr<FDisplayClusterConfiguratorToolbar> GetConfiguratorToolbar() const { return ConfiguratorToolbar; }

	/** Syncs shared properties between viewports. */
	void SyncViewports();
	
	/** Make sure the blueprint preview actor is up to date. */
	void RefreshDisplayClusterPreviewActor(bool bFullRefresh = false);

	/** Output mapping preview will update the next tick. */
	void RequestOutputMappingPreviewUpdate();

	/** Reselects selected objects. Useful if recompiling as sometimes the details panel loses focus. */
	void ReselectObjects();

	/** Restores previously open documents. */
	void RestoreLastEditedState();

protected:
	/** Applies preview texture to all output mapping viewport nodes. */
	void UpdateOutputMappingPreview();
	
	/** Clears preview texture from all output mapping viewport nodes. */
	void CleanupOutputMappingPreview();

private:
	TWeakObjectPtr<AActor> CurrentPreviewActor;

	// logic require signed number
	int16 TicksForPreviewRenderCapture = -1;
	
	FDelegateHandle UpdateOutputMappingHandle;

public:
	// Load with OpenFileDialog
	bool LoadWithOpenFileDialog();

	// Load from specified file
	bool LoadFromFile(const FString& FilePath);

	// Save to the same file the config data was read from
	bool ExportConfig();

	// Verifies config can be saved.
	bool CanExportConfig() const;
	
	// Save to a specified file
	bool SaveToFile(const FString& FilePath);

	// Save with SaveFileDialog
	bool SaveWithOpenFileDialog();

protected:

	//~ Begin IAssetEditorInstance Interface
	virtual FName GetEditorName() const override { return TEXT("DisplayClusterConfigurator"); }
	//~ End IAssetEditorInstance Interface

	//~ Begin FBlueprintEditor Interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void SaveAsset_Execute() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual TStatId GetStatId() const override;
	virtual bool OnRequestClose() override;
	virtual void OnBlueprintChangedImpl(UBlueprint* InBlueprint, bool bIsJustBeingCompiled) override;
	//~ End FBlueprintEditor Interface

	// SSCS Implementation
	/** Delegate invoked when the selection is changed in the SCS editor widget */
	virtual void OnSelectionUpdated(const TArray<TSharedPtr<class FSCSEditorTreeNode>>& SelectedNodes) override;

	/** Delegate invoked when an item is double clicked in the SCS editor widget */
	virtual void OnComponentDoubleClicked(TSharedPtr<class FSCSEditorTreeNode> Node) override;

	friend struct FDisplayClusterSCSViewportSummoner;
	void CreateDCSCSEditors();
	void ShutdownDCSCSEditors();

	TSharedRef<SWidget> CreateSCSEditorExtensionWidget(FWeakObjectPtr ExtensionContext);

	void CreateSCSEditorWrapper();

private:
	/** True only during SCS selection change. */
	bool bSCSEditorSelecting;

	/** Indicates that the current selection change in the SCS is silent, meaning it doesn't broadcast a selection changed event or update the inspector. */
	bool bSelectSilently;
	// ~End of SSCS Implementation

protected:
	void CreateWidgets();

	void ExtendMenu();

	void ExtendToolbar();

	void OnPostCompiled(UBlueprint* InBlueprint);

private:
	//~ Begin UI command handlers
	void ImportConfig_Clicked();

	void ExportToFile_Clicked();

	void EditConfig_Clicked();

	bool IsExportOnSaveSet() const;
	void ToggleExportOnSaveSetting();
	//~ End UI command handlers

	void OnReadOnlyChanged(bool bReadOnly);
	void OnRenameVariable(UBlueprint* Blueprint, UClass* VariableClass, const FName& OldVariableName, const FName& NewVariableName);

	void BindCommands();

	// Updates the ConfigExport property of the LoadedBlueprint
	void UpdateConfigExportProperty();

private:
	TSharedPtr<FDisplayClusterConfiguratorViewGeneral> ViewGeneral;
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping;
	TSharedPtr<FDisplayClusterConfiguratorViewCluster> ViewCluster;
	TSharedPtr<FDisplayClusterConfiguratorViewInput> ViewInput; // TODO: Delete this after input plugin finished.

	/** Owner of the viewport. */
	TSharedPtr<SDockTab> ViewportTab;
	/* Tracking the active viewports in this editor. */
	TSharedPtr<FEditorViewportTabContent> ViewportTabContent;

	TSharedPtr<SWidget> SCSEditorWrapper;

	TSharedPtr<FExtender> MenuExtender;
	TSharedPtr<FExtender> ToolbarExtender;

	TSharedPtr<FDisplayClusterConfiguratorToolbar> ConfiguratorToolbar;
	
	FOnConfigReloaded OnConfigReloaded;

	/** Delegate for when an item is selected */
	FOnObjectSelected OnObjectSelected;

	/** View invalidation delegate */
	FOnInvalidateViews OnInvalidateViews;

	FOnClearViewportSelection OnClearViewportSelection;

	/** Delegate which is raised when the cluster configuration is changed. */
	FOnClusterChanged OnClusterChanged;

	TArray<UObject*> SelectedObjects;

	/** The currently loaded blueprint. */
	TWeakObjectPtr<UDisplayClusterBlueprint> LoadedBlueprint;

	FName SCSEditorExtensionIdentifier;

	FDelegateHandle RenameVariableHandle;
};
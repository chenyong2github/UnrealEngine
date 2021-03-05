// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Framework/Docking/TabManager.h"


class SDockTab;
class SRemoteControlPanel;
class SRCPanelInputBindings;
struct SRCPanelTreeNode;

/** Viewer/editor for a Remote Control Preset */
class FRemoteControlPresetEditorToolkit : public FAssetEditorToolkit
{
public:
	/**
	 *  Create an editor for a remote control preset asset.
	 */
	static TSharedRef<FRemoteControlPresetEditorToolkit> CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, class URemoteControlPreset* InPreset);

	/**
	 * Initialize a remote control preset editor module. 
	 */
	void InitRemoteControlPresetEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, class URemoteControlPreset* InPreset);
	
	//~ Begin IToolkit interface
	virtual FText GetBaseToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual bool OnRequestClose() override;
	//~ End IToolkit interface

private:
	/** Handle spawning the tab that holds the remote control panel tab. */
	TSharedRef<SDockTab> HandleTabManagerSpawnPanelTab(const FSpawnTabArgs& Args);

	/** Handle spawning the tab the holds the input bindings tab. */
	TSharedRef<SDockTab> HandleTabManagerSpawnInputBindingsTab(const FSpawnTabArgs& Args);

	//~ Handle assigning group selection in either tab.
	void OnPanelSelectionChange(const TSharedPtr<SRCPanelTreeNode>& Node);
	void OnInputBindingsSelectionChange(const TSharedPtr<SRCPanelTreeNode>& Node);
private:
	/** Holds the remote control panel tab id. */
	static const FName PanelTabId;
	/** Holds the input bindings tab id. */
	static const FName InputBindingsTabId;
	/** Holds the remote control panel app identifier. */
	static const FName RemoteControlPanelAppIdentifier;
	/** Holds the preset being edited. */
	URemoteControlPreset* Preset = nullptr;
	/** Holds the panel widget */
	TSharedPtr<SRemoteControlPanel> PanelTab;
	/** Holds the input bindings widget */
	TSharedPtr<SRCPanelInputBindings> InputBindingsTab;
};
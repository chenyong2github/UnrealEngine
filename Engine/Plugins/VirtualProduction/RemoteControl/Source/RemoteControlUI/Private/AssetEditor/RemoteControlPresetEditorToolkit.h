// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Framework/Docking/TabManager.h"

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
	/** Handle spawning the tabs that holds the remote control panel. */
	TSharedRef<class SDockTab> HandleTabManagerSpawnTab(const FSpawnTabArgs& Args);
private:
	/** Holds the remote control panel tab id. */
	static const FName RemoteControlPanelTabId;

	/** Holds the remote control panel app identifier. */
	static const FName RemoteControlPanelAppIdentifier;

	TSharedPtr<class SRemoteControlPanel> PanelWidget;
};
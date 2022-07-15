// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"
#include "Widgets/Docking/SDockTab.h"

class FObjectMixerEditorMainPanel;

class OBJECTMIXEREDITOR_API FObjectMixerEditorModule : public IModuleInterface
{
public:

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface 

	static FObjectMixerEditorModule& Get();
	
	virtual TSharedPtr<SWidget> MakeObjectMixerDialog() const;

	/*
	 * Regenerate the list items and refresh the list. Call when adding or removing variables.
	 * @param bShouldCacheValues If true, the current list's current values will be cached and then restored when the list is rebuilt. Otherwise preset values will be used.
	 */
	virtual void RebuildList(const FString InItemToScrollTo = "", bool bShouldCacheValues = true) const;
	
	/**
	 * Refresh filters and sorting.
	 * Useful for when the list state has gone stale but the variable count has not changed.
	 */
	virtual void RefreshList() const;

	void RegisterMenuGroupAndTabSpawner();
	/**
	 * Add a tab spawner to the Object Mixer menu group.
	 * @return If adding the item to the menu was successful
	 */
	bool RegisterItemInMenuGroup(FWorkspaceItem& InItem);
	
	virtual void UnregisterTabSpawner();
	virtual void RegisterProjectSettings() const;
	virtual void UnregisterProjectSettings() const;

	virtual TSharedRef<SDockTab> SpawnMainPanelTab(const FSpawnTabArgs& Args);
	
	static const FName ObjectMixerToolkitPanelTabId;

	TSharedPtr<FWorkspaceItem> GetWorkspaceGroup();

protected:
	
	/** Lives for as long as the module is loaded. */
	TSharedPtr<FObjectMixerEditorMainPanel> MainPanel;

	/** The text that appears on the spawned nomad tab */
	FText TabLabel;

	// If set, this is the filter class used to initialize the MainPanel
	TSubclassOf<UObjectMixerObjectFilter> DefaultFilterClass;

private:

	TSharedPtr<FWorkspaceItem> WorkspaceGroup;
};

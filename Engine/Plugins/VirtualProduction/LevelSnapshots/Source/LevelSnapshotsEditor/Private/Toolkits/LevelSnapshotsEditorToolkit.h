// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Views/SnapshotEditorViewData.h"
#include "Toolkits/AssetEditorToolkit.h"

class FLevelSnapshotsEditorInput;
class FLevelSnapshotsEditorFilters;
class FLevelSnapshotsEditorResults;
class FLevelSnapshotsEditorContext;
class SCheckBox;
class SDockTab;
class ULevelSnapshotsEditorData;

struct FSnapshotEditorViewData;

class ULevelSnapshot;

class FLevelSnapshotsEditorToolkit
	:
	public FAssetEditorToolkit
{
	using Super = FAssetEditorToolkit;
public:

	static const FName AppIdentifier;
	static const FName ToolbarTabId;
	static const FName InputTabID;
	static const FName FilterTabID;
	static const FName ResultsTabID;
	
	static TSharedPtr<FLevelSnapshotsEditorToolkit> CreateSnapshotEditor(ULevelSnapshotsEditorData* EditorData);

	void OpenLevelSnapshotsDialogWithAssetSelected(const FAssetData& InAssetData) const;

	//~ Begin FBaseToolkit Interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual FText GetToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FText GetBaseToolkitName() const override { return FText::GetEmpty(); }
	virtual FName GetToolkitFName() const override { return FName("Level Snapshots"); }
	virtual FString GetWorldCentricTabPrefix() const override { return FString(); }
	virtual bool IsAssetEditor() const override { return false; }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor::White; }
	//~ End FBaseToolkit Interface

	virtual ~FLevelSnapshotsEditorToolkit() = default;

private:

	void Initialize(ULevelSnapshotsEditorData* EditorData);

	TSharedRef<SDockTab> SpawnTab_CustomToolbar(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Input(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Filter(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Results(const FSpawnTabArgs& Args);

	FReply OnClickTakeSnapshot();
	FReply InputPanelExpandClicked();
	
	TWeakObjectPtr<ULevelSnapshotsEditorData> EditorData;
	
	FSnapshotEditorViewData ViewBuildData;

	TSharedPtr<FLevelSnapshotsEditorInput> EditorInput;
	TSharedPtr<FLevelSnapshotsEditorFilters> EditorFilters;
	TSharedPtr<FLevelSnapshotsEditorResults> EditorResults;

	TSharedPtr<SCheckBox> SettingsButtonPtr;

	TSharedPtr<SDockTab> InputTab;
	bool bInputPanelExpanded = true;
};

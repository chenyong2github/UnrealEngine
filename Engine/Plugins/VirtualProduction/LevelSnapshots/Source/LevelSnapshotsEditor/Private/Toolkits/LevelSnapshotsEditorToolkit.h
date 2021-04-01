// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Toolkits/BaseToolkit.h"

class ULevelSnapshotsEditorData;

class FLevelSnapshotsEditorInput;
class FLevelSnapshotsEditorFilters;
class FLevelSnapshotsEditorResults;
class FLevelSnapshotsEditorContext;
struct FLevelSnapshotsEditorViewBuilder;

class ULevelSnapshot;

class FLevelSnapshotsEditorToolkit
	: public FBaseToolkit,
	public TSharedFromThis<FLevelSnapshotsEditorToolkit>
{
public:

	static const FName ToolbarTabId;
	static const FName InputTabID;
	static const FName FilterTabID;
	static const FName ResultsTabID;
	
	static TSharedPtr<FLevelSnapshotsEditorToolkit> CreateSnapshotEditor(ULevelSnapshotsEditorData* EditorData);
	
	void ShowEditor();
	void BringToFront();

	//~ Begin FBaseToolkit Interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	// The functions below are non-interesting but required:
	virtual FName GetToolkitFName() const override { return "Levelsnapshots"; }
	virtual FText GetBaseToolkitName() const override { return FText::GetEmpty(); }
	virtual FText GetToolkitName() const override { return FText::GetEmpty(); }
	virtual FText GetToolkitToolTipText() const override { return FText::GetEmpty(); }
	virtual FString GetWorldCentricTabPrefix() const override { return FString(); }
	virtual bool IsAssetEditor() const override { return false; }
	virtual const TArray<UObject*>* GetObjectsCurrentlyBeingEdited() const override { return {}; }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor::White; }
	virtual FEdMode* GetEditorMode() const override { return nullptr; }
	virtual UEdMode* GetScriptableEditorMode() const override { return nullptr; }
	virtual FText GetEditorModeDisplayName() const override { return FText::GetEmpty(); }
	virtual FSlateIcon GetEditorModeIcon() const override { return FSlateIcon(); }
	//~ End FBaseToolkit Interface

	virtual ~FLevelSnapshotsEditorToolkit() = default;

private:

	void Initialize(ULevelSnapshotsEditorData* EditorData);

	TSharedRef<SDockTab> SpawnTab_Toolbar(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Input(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Filter(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Results(const FSpawnTabArgs& Args);

	FReply OnClickTakeSnapshot();
	FReply OnClickApplyToWorld();

	bool OnRequestClose();


	
	TWeakObjectPtr<ULevelSnapshotsEditorData> EditorData;

	/* Root of the editor */
	TSharedPtr<SDockTab> MajorTabRoot;
	/* Registered with toolkit manager. Keeps the editor instance alive. */
	TSharedPtr<class SLevelSnapshotsEditorHost> StandaloneHost;
	TSharedPtr<FTabManager> TabManager;
	
	TSharedPtr<FLevelSnapshotsEditorViewBuilder> ViewBuilder;

	TSharedPtr<FLevelSnapshotsEditorInput> EditorInput;
	TSharedPtr<FLevelSnapshotsEditorFilters> EditorFilters;
	TSharedPtr<FLevelSnapshotsEditorResults> EditorResults;

	TSharedPtr<FLevelSnapshotsEditorContext> EditorContext;
	
};

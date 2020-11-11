// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILevelSnapshotsEditorToolkit.h"

class ULevelSnapshotsEditorData;

class FLevelSnapshotsEditorInput;
class FLevelSnapshotsEditorFilters;
class FLevelSnapshotsEditorResults;
class FLevelSnapshotsEditorContext;
struct FLevelSnapshotsEditorViewBuilder;

class ULevelSnapshot;

class FLevelSnapshotsEditorToolkit
	: public ILevelSnapshotsEditorToolkit
{
public:
	FLevelSnapshotsEditorToolkit();

	/** Virtual destructor */
	virtual ~FLevelSnapshotsEditorToolkit();

	/**
	 * Initialize this asset editor.
	 *
	 * @param Mode Asset editing mode for this editor (standalone or world-centric).
	 * @param InitToolkitHost When Mode is WorldCentric, this is the level editor instance to spawn this editor within.
	 * @param LevelSequence The animation to edit.
	 * @param TrackEditorDelegates Delegates to call to create auto-key handlers for this sequencer.
	 */
	void Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, ULevelSnapshotsEditorData* EditorData);

	virtual FName GetToolkitFName() const override
	{
		return NAME_None;
	}

	virtual FText GetBaseToolkitName() const override
	{
		return FText::GetEmpty();
	}

	virtual FString GetWorldCentricTabPrefix() const override
	{
		return TEXT("");
	}

	virtual FLinearColor GetWorldCentricTabColorScale() const override
	{
		return FLinearColor::White;
	}


private:
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

	TSharedRef<SDockTab> SpawnTab_Input(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Filter(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Results(const FSpawnTabArgs& Args);

	/** Builds the toolbar widget */
	void ExtendToolbar();
	void SetupCommands();

	void ApplyToWorld();

	void SnapshotSelected(ULevelSnapshot* InLevelSnapshot);

private:
	TSharedPtr<FLevelSnapshotsEditorViewBuilder> ViewBuilder;

	ULevelSnapshotsEditorData* EditorData;

	TWeakObjectPtr<ULevelSnapshot> ActiveLevelSnapshotPtr;

	TSharedPtr<FLevelSnapshotsEditorInput> EditorInput;
	TSharedPtr<FLevelSnapshotsEditorFilters> EditorFilters;
	TSharedPtr<FLevelSnapshotsEditorResults> EditorResults;

	TSharedPtr<FLevelSnapshotsEditorContext> EditorContext;

public:
	static const FName InputTabID;
	static const FName FilterTabID;
	static const FName ResultsTabID;
};

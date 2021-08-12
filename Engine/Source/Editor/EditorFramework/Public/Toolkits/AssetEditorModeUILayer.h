// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Editor.h"
#include "ILevelEditor.h"
#include "Misc/NotifyHook.h"
#include "StatusBarSubsystem.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"

class FExtender;
class SBorder;
class IToolkit;
class SDockTab;
class ILevelEditor;


class EDITORFRAMEWORK_API FAssetEditorModeUILayer : public TSharedFromThis<FAssetEditorModeUILayer>
{
public:
	FAssetEditorModeUILayer(const IToolkitHost* InToolkitHost);
	FAssetEditorModeUILayer() {};
	virtual ~FAssetEditorModeUILayer() {};
	/** Called by SLevelEditor to notify the toolbox about a new toolkit being hosted */
	virtual void OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit);

	/** Called by SLevelEditor to notify the toolbox about an existing toolkit no longer being hosted */
	virtual void OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit);
	virtual TSharedPtr<FTabManager> GetTabManager();
	virtual TSharedPtr<FWorkspaceItem> GetModeMenuCategory();
	virtual void SetModePanelInfo(const FName InTabSpawnerID, const FMinorTabConfig& InTabInfo);
	virtual TMap<FName, TWeakPtr<SDockTab>> GetSpawnedTabs();
	virtual FSimpleDelegate& ToolkitHostReadyForUI()
	{
		return OnToolkitHostReadyForUI;
	};
	virtual const FName GetStatusBarName() const
	{
		return NAME_None;
	}
public:
	static const FName VerticalToolbarID;
	static const FName TopLeftTabID;
	static const FName BottomLeftTabID;
	static const FName TopRightTabID;
	static const FName BottomRightTabID;

protected:
	const FOnSpawnTab& GetStoredSpawner(const FName TabID);
	void RegisterModeTabSpawners();
	void RegisterModeTabSpawner(const FName TabID);
	TSharedRef<SDockTab> SpawnStoredTab(const FSpawnTabArgs& Args, const FName TabID);
	bool CanSpawnStoredTab(const FSpawnTabArgs& Args, const FName TabID);
	FText GetTabSpawnerName(const FName TabID) const;
	FText GetTabSpawnerTooltip(const FName TabID) const;
	virtual void RegisterLayoutExtensions(FLayoutExtender& Extender) {};

protected:
	/** The host of the toolkits created by modes */
	const IToolkitHost* ToolkitHost;
	TArray<FName> ModeTabIDs;
	TWeakPtr<IToolkit> HostedToolkit;
	TMap<FName, FMinorTabConfig> RequestedTabInfo;
	TMap<FName, TWeakPtr<SDockTab>> SpawnedTabs;
	FSimpleDelegate OnToolkitHostReadyForUI;
};

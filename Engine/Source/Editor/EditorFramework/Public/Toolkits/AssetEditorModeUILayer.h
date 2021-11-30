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
#include "AssetEditorModeUILayer.generated.h"

class FExtender;
class SBorder;
class IToolkit;
class SDockTab;
class ILevelEditor;

UCLASS()
class EDITORFRAMEWORK_API UAssetEditorUISubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	static const FName VerticalToolbarID;
	static const FName TopLeftTabID;
	static const FName BottomLeftTabID;
	static const FName TopRightTabID;
	static const FName BottomRightTabID;

protected:
	virtual void RegisterLayoutExtensions(FLayoutExtender& Extender) {};
};

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
	virtual TSharedPtr<FWorkspaceItem> GetModeMenuCategory() const;
	virtual void SetModePanelInfo(const FName InTabSpawnerID, const FMinorTabConfig& InTabInfo);
	virtual TMap<FName, TWeakPtr<SDockTab>> GetSpawnedTabs();
	virtual FSimpleDelegate& ToolkitHostReadyForUI()
	{
		return OnToolkitHostReadyForUI;
	};
	virtual FSimpleDelegate& ToolkitHostShutdownUI()
	{
		return OnToolkitHostShutdownUI;
	}
	virtual const FName GetStatusBarName() const
	{
		return ToolkitHost->GetStatusBarName();
	}

protected:
	const FOnSpawnTab& GetStoredSpawner(const FName TabID);
	void RegisterModeTabSpawners();
	void RegisterModeTabSpawner(const FName TabID);
	TSharedRef<SDockTab> SpawnStoredTab(const FSpawnTabArgs& Args, const FName TabID);
	bool CanSpawnStoredTab(const FSpawnTabArgs& Args, const FName TabID);
	FText GetTabSpawnerName(const FName TabID) const;
	FText GetTabSpawnerTooltip(const FName TabID) const;
	const FSlateIcon& GetTabSpawnerIcon(const FName TabID) const;

protected:
	/** The host of the toolkits created by modes */
	const IToolkitHost* ToolkitHost;
	TArray<FName> ModeTabIDs;
	TWeakPtr<IToolkit> HostedToolkit;
	TMap<FName, FMinorTabConfig> RequestedTabInfo;
	TMap<FName, TWeakPtr<SDockTab>> SpawnedTabs;
	FSimpleDelegate OnToolkitHostReadyForUI;
	FSimpleDelegate OnToolkitHostShutdownUI;
};

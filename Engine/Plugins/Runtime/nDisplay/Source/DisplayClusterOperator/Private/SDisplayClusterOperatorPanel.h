// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FTabManager;
class FLayoutExtender;
class FSpawnTabArgs;
class FMenuBuilder;
class SBox;
class SDockTab;
class SWindow;
class SDisplayClusterOperatorToolbar;

/** The nDisplay operator panel that allows users to edit root actor instances with a variety of tools */
class SDisplayClusterOperatorPanel : public SCompoundWidget
{
public:
	/** The name of the tab that the operator panel lives in */
	static const FName TabName;

	/** The tab ID that the operator toolbar lives in */
	static const FName ToolbarTabId;

	/** The tab ID that the details panel lives in */
	static const FName DetailsTabId;

	/** The ID of the tab stack that can be extended by external tabs */
	static const FName TabExtensionId;


	/** Registers the operator panel with the global tab manager and adds it to the Virtual Projections window menu */
	static void RegisterTabSpawner();

	/** Unregisters the operator panel from the global tab manager */
	static void UnregisterTabSpawner();

	/** Creates a tab with the operator panel inside */
	static TSharedRef<SDockTab> SpawnInTab(const FSpawnTabArgs& SpawnTabArgs);


	SLATE_BEGIN_ARGS(SDisplayClusterOperatorPanel) {}
	SLATE_END_ARGS()

	~SDisplayClusterOperatorPanel();

	void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& MajorTabOwner, const TSharedPtr<SWindow>& WindowOwner);

private:
	/** Creates a tab with the operator toolbar in it */
	TSharedRef<SDockTab> SpawnToolbarTab(const FSpawnTabArgs& Args);

	/** Creates a tab with the details panel in it */
	TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args);

	/** Raised when something wants to display a list of objects in the operator's details panel */
	void DisplayObjectsInDetailsPanel(const TArray<UObject*>& Objects);

private:
	/** Holds the tab manager that manages the operator's tabs. */
	TSharedPtr<FTabManager> TabManager;

	/** The layout extender used by the operator's layout */
	TSharedPtr<FLayoutExtender> LayoutExtender;

	/** The container for the toolbar */
	TSharedPtr<SBox> ToolbarContainer;
	
	/** A reference to the operator panel's toolbar widget */
	TSharedPtr<SDisplayClusterOperatorToolbar> Toolbar;

	/** A reference to the operator panel's details view */
	TSharedPtr<class SKismetInspector> DetailsView;

	/** The delegate handle for the operator module's OnDetailObjectsChanged event */
	FDelegateHandle DetailObjectsChangedHandle;
};
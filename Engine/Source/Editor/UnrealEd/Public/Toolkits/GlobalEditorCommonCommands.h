// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Docking/TabManager.h"

class IMenu;
class SWindow;

// Global editor common commands
// Note: There is no real global command concept, so these must still be registered in each editor
class FGlobalEditorCommonCommands : public TCommands< FGlobalEditorCommonCommands >
{
public:
	UNREALED_API FGlobalEditorCommonCommands();
	UNREALED_API ~FGlobalEditorCommonCommands();

	UNREALED_API virtual void RegisterCommands() override;

	static UNREALED_API void MapActions(TSharedRef<FUICommandList>& ToolkitCommands);

protected:
	static UNREALED_API void OnPressedCtrlTab(TSharedPtr<FUICommandInfo> TriggeringCommand);
	static UNREALED_API void OnSummonedAssetPicker();
	static UNREALED_API void OnSummonedConsoleCommandBox();
	static UNREALED_API void OnOpenContentBrowserDrawer();
	static UNREALED_API void OnOpenOutputLogDrawer();

	static UNREALED_API TSharedRef<SDockTab> SpawnAssetPicker(const FSpawnTabArgs& InArgs);

	static UNREALED_API TSharedPtr<IMenu> OpenPopupMenu(TSharedRef<SWidget> WindowContents, const FVector2D& PopupDesiredSize);
public:
	TSharedPtr<FUICommandInfo> FindInContentBrowser;
	TSharedPtr<FUICommandInfo> SummonControlTabNavigation;
	TSharedPtr<FUICommandInfo> SummonControlTabNavigationAlternate;
	TSharedPtr<FUICommandInfo> SummonControlTabNavigationBackwards;
	TSharedPtr<FUICommandInfo> SummonControlTabNavigationBackwardsAlternate;
	TSharedPtr<FUICommandInfo> SummonOpenAssetDialog;
	TSharedPtr<FUICommandInfo> SummonOpenAssetDialogAlternate;
	TSharedPtr<FUICommandInfo> OpenDocumentation;
	TSharedPtr<FUICommandInfo> OpenConsoleCommandBox;
	TSharedPtr<FUICommandInfo> SelectNextConsoleExecutor;
	TSharedPtr<FUICommandInfo> OpenOutputLogDrawer;
	TSharedPtr<FUICommandInfo> OpenContentBrowserDrawer;
};


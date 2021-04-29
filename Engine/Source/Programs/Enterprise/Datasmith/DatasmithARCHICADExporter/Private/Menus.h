// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/AddonTools.h"

BEGIN_NAMESPACE_UE_AC

class FMenus
{
  public:
	// Add menu to the menu bar and also add an item to palette menu
	static GSErrCode Register();

	// Enable handlers of menu items
	static GSErrCode Initialize();

	// Ename or disable menu item
	static void SetMenuItemStatus(short InMenu, short InItem, bool InSet, GSFlags InFlag);

	// Change the text of an item
	static void SetMenuItemText(short InMenu, short InItem, const GS::UniString& ItemStr);

	// LiveLink status changed
	static void LiveLinkChanged();

	// Schedule a Live Link snapshot to be executed from the main thread event loop.
	static void PostDoSnapshot();

  private:
	// Menu command handler
	static GSErrCode __ACENV_CALL MenuCommandHandler(const API_MenuParams* MenuParams) noexcept;

	// Process menu command
	static GSErrCode DoMenuCommand(void* InMenuParams, void*);

	// Intra add-ons command handler
	static GSErrCode __ACENV_CALL SyncCommandHandler(GSHandle ParHdl, GSPtr ResultData, bool SilentMode) noexcept;

	// Process intra add-ons command
	static GSErrCode DoSyncCommand(void*, void*);
};

END_NAMESPACE_UE_AC

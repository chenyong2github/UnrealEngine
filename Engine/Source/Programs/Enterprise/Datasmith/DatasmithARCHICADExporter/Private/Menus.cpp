// Copyright Epic Games, Inc. All Rights Reserved.

#include "Menus.h"
#include "ResourcesIDs.h"
#include "Error.h"
#include "Commander.h"

BEGIN_NAMESPACE_UE_AC

enum : GSType
{
	DatasmithDynamicLink = 'DsDL'
}; // Can be called by another Add-on

// Add menu to the menu bar and also add an item to palette menu
inline static void RegisterMenu(GSErrCode* IOGSErr, short InMenuResId, short InMenuHelpResId, APIMenuCodeID InMenuCode,
								GSFlags InMenuFlags)
{
	if (*IOGSErr == NoError)
	{
		*IOGSErr =
			ACAPI_Register_Menu(LocalizeResId(InMenuResId), LocalizeResId(InMenuHelpResId), InMenuCode, InMenuFlags);
	}
}

#define RegisterMenuAndHelp(IOGSErr, MenuId, MenuCode, MenuFlags) \
	RegisterMenu(IOGSErr, MenuId, MenuId##Help, MenuCode, MenuFlags)

// Add menu to the menu bar and also add an item to palette menu
GSErrCode FMenus::Register()
{
	GSErrCode GSErr = NoError;

	RegisterMenuAndHelp(&GSErr, kStrListMenuDatasmith, MenuCode_Palettes, MenuFlag_Default);

	RegisterMenuAndHelp(&GSErr, kStrListMenuItemSnapshot, MenuCode_UserDef, MenuFlag_Default);
	RegisterMenuAndHelp(&GSErr, kStrListMenuItemConnections, MenuCode_UserDef, MenuFlag_Default);
	RegisterMenuAndHelp(&GSErr, kStrListMenuItemExport, MenuCode_UserDef, MenuFlag_Default);
	RegisterMenuAndHelp(&GSErr, kStrListMenuItemMessages, MenuCode_UserDef, MenuFlag_Default);
	RegisterMenuAndHelp(&GSErr, kStrListMenuItemPalette, MenuCode_UserDef, MenuFlag_SeparatorBefore);
	RegisterMenuAndHelp(&GSErr, kStrListMenuItemAbout, MenuCode_UserDef, MenuFlag_Default);

	if (GSErr == NoError)
	{
		GSErr = ACAPI_Register_SupportedService(DatasmithDynamicLink, 1L);
	}

	return GSErr;
}

// Enable handlers of menu items
GSErrCode FMenus::Initialize()
{
	GSErrCode GSErr = NoError;
	for (short IndexMenu = kStrListMenuItemSnapshot; IndexMenu <= kStrListMenuItemAbout && GSErr == NoError;
		 IndexMenu++)
	{
		GSErr = ACAPI_Install_MenuHandler(LocalizeResId(IndexMenu), MenuCommandHandler);
	}
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FMenus::Initialize - ACAPI_Install_MenuHandler error=%d\n", GSErr);
	}
	GSErr = ACAPI_Install_MenuHandler(LocalizeResId(kStrListMenuDatasmith), MenuCommandHandler);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FMenus::Initialize - ACAPI_Install_MenuHandler error=%d\n", GSErr);
	}
	GSErr = ACAPI_Install_ModulCommandHandler(DatasmithDynamicLink, 1L, SyncCommandHandler);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FMenus::Initialize - ACAPI_Install_ModulCommandHandler error=%d\n", GSErr);
	}
	return GSErr;
}

// Ename or disable menu item
void FMenus::SetMenuItemStatus(short InMenu, short InItem, bool InEnabled)
{
	API_MenuItemRef ItemRef;
	Zap(&ItemRef);
	ItemRef.menuResID = LocalizeResId(InMenu);
	ItemRef.itemIndex = InItem;
	GSFlags	  ItemFlags = 0;
	GSErrCode GSErr = ACAPI_Interface(APIIo_GetMenuItemFlagsID, &ItemRef, &ItemFlags);
	if (GSErr == NoError)
	{
		if (InEnabled)
		{
			ItemFlags &= ~API_MenuItemDisabled;
		}
		else
		{
			ItemFlags |= API_MenuItemDisabled;
		}
		GSErr = ACAPI_Interface(APIIo_SetMenuItemFlagsID, &ItemRef, &ItemFlags);
		if (GSErr != NoError)
		{
			UE_AC_TraceF("FMenus::SetMenuItemStatus - APIIo_SetMenuItemFlagsID error=%d\n", GSErr);
		}
	}
	else
	{
		UE_AC_TraceF("FMenus::SetMenuItemStatus - APIIo_GetMenuItemFlagsID error=%d\n", GSErr);
	}
}

// Change the text of an item
void FMenus::SetMenuItemText(short InMenu, short InItem, const GS::UniString& ItemStr)
{
	API_MenuItemRef ItemRef;
	Zap(&ItemRef);
	ItemRef.menuResID = LocalizeResId(InMenu);
	ItemRef.itemIndex = InItem;
	GSErrCode GSErr =
		ACAPI_Interface(APIIo_SetMenuItemTextID, &ItemRef, nullptr, const_cast< GS::UniString* >(&ItemStr));
	if (GSErr != NoError)
	{
		UE_AC_TraceF("FMenus::SetMenuItemText - APIIo_SetMenuItemTextID error=%d\n", GSErr);
	}
}

// Menu command handler
GSErrCode __ACENV_CALL FMenus::MenuCommandHandler(const API_MenuParams* MenuParams) noexcept
{
	return TryFunction("FMenus::DoMenuCommand", FMenus::DoMenuCommand, (void*)MenuParams);
}

// Process menu command
GSErrCode FMenus::DoMenuCommand(void* InMenuParams, void*)
{
	const API_MenuParams& MenuParams = *reinterpret_cast< const API_MenuParams* >(InMenuParams);

	short MenuId = MenuParams.menuItemRef.menuResID - LocalizeResId(0);
	if (MenuParams.menuItemRef.itemIndex != 1)
	{
		UE_AC_DebugF("FMenus::DoMenuCommand - Menu %d, Item is %d\n", MenuId, MenuParams.menuItemRef.itemIndex);
	}

	switch (MenuId)
	{
		case kStrListMenuItemSnapshot:
			FCommander::DoSnapshot();
			break;
		case kStrListMenuItemConnections:
			FCommander::ShowConnectionsDialog();
			break;
		case kStrListMenuItemExport:
			FCommander::Export3DToFile();
			break;
		case kStrListMenuItemMessages:
			FCommander::ShowMessagesDialog();
			break;
		case kStrListMenuItemPalette:
			FCommander::ShowHidePalette();
			break;
		case kStrListMenuItemAbout:
			FCommander::ShowAboutOf();
			break;
		case kStrListMenuDatasmith:
			FCommander::ShowHidePalette();
			break;
	}
	return GS::NoError;
}

// Intra add-ons command handler
GSErrCode __ACENV_CALL FMenus::SyncCommandHandler(GSHandle /* ParHdl */, GSPtr /* ResultData */,
												  bool /* silentMode */) noexcept
{
	return TryFunction("FMenus::DoSyncCommand", FMenus::DoSyncCommand);
}

// Process intra add-ons command
GSErrCode FMenus::DoSyncCommand(void*, void*)
{
	FCommander::DoSnapshot();
	return GS::NoError;
}

END_NAMESPACE_UE_AC

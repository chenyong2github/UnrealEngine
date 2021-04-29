// Copyright Epic Games, Inc. All Rights Reserved.

#include "Palette.h"
#include "Preferences.h"
#include "Utils/TAssValueName.h"
#include "ResourcesIDs.h"
#include "Utils/Error.h"
//#include "CSynchronizer.hpp"
#include "Commander.h"
#include "Menus.h"

#define PALETTE_4_ALL_VIEW 1
#define TRACE_PALETTE 0

BEGIN_NAMESPACE_UE_AC

enum
{
	kDial_Snapshot = 1,
	kDial_StartLiveLink,
	kDial_PauseLiveLink,
	kDial_Connections,
	kDial_Export3D,
	kDial_Messages,

	kDial_Information,
	kDial_ZapModelDB
};

static bool bPaletteRegistered = false;

void FPalette::Register()
{
	if (bPaletteRegistered)
	{
		return;
	}
#if PALETTE_4_ALL_VIEW
	GS::GSFlags Flags = API_PalEnabled_FloorPlan | API_PalEnabled_Section | API_PalEnabled_3D | API_PalEnabled_Detail |
						API_PalEnabled_Layout | API_PalEnabled_Worksheet | API_PalEnabled_Elevation |
						API_PalEnabled_InteriorElevation | API_PalEnabled_DocumentFrom3D;
#else
	GS::GSFlags Flags = API_PalEnabled_3D;
#endif
	GSErrCode GSErr = ACAPI_RegisterModelessWindow(LocalizeResId(kDlgPalette), APIPaletteControlCallBack, Flags,
												   GSGuid2APIGuid(PaletteGuid));
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FPalette::Register - ACAPI_RegisterModelessWindow failed err(%d)\n", GSErr);
	}
	else
	{
		bPaletteRegistered = true;
	}
}

void FPalette::Unregister()
{
	if (bPaletteRegistered)
	{
		GSErrCode GSErr = ACAPI_UnregisterModelessWindow(LocalizeResId(kDlgPalette));
		if (GSErr != NoError)
		{
			UE_AC_DebugF("FPalette::Unregister - ACAPI_UnregisterModelessWindow failed err(%d)\n", GSErr);
		}
		bPaletteRegistered = false;
	}
}

// Toggle visibility of palette
void FPalette::ShowFromUser()
{
	// No palette, we create one
	if (CurrentPalette == nullptr)
	{
		FPreferences::Get()->Prefs.Palette.bHiddenByUser = false;
		FPalette::Create();
		if (CurrentPalette->DialId == 0) // Got problem?
			delete CurrentPalette;
		else
		{
			WindowChanged();
		}
	}
	else
	{
		if (CurrentPalette->State.bHiddenByUser)
		{
			CurrentPalette->ShowHide(true, false);
		}
		else
		{
			CurrentPalette->State.bHiddenByUser = true;
			CurrentPalette->Save2Pref();
			FPalette::Delete();
			SetPaletteMenuTexts(false, true);
		}
	}
}

// Return true if 3d window is the current
bool FPalette::Is3DCurrenWindow()
{
	API_WindowInfo WindowInfo;
	Zap(&WindowInfo);
	GSErrCode GSErr = ACAPI_Database(APIDb_GetCurrentWindowID, &WindowInfo);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FPalette::Is3DCurrenWindow - APIDb_GetCurrentWindowID error=%s\n", GetErrorName(GSErr));
	}
	return WindowInfo.typeID == APIWind_3DModelID;
}

// Switch to another window
void FPalette::WindowChanged()
{
	bool bIs3DView = Is3DCurrenWindow();

#if PALETTE_4_ALL_VIEW
	if (CurrentPalette != nullptr)
	{
		DGSetItemEnable(CurrentPalette->DialId, kDial_Snapshot, bIs3DView);
	}
#endif
	//	FMenus::SetMenuItemStatus(kStrListMenuItemSync, 1, bIs3DView);
}

// LiveLink status changed
void FPalette::LiveLinkChanged()
{
	bool bLiveLinkEnabled = FCommander::IsLiveLinkEnabled();
	DGSetItemVisible(CurrentPalette->DialId, kDial_StartLiveLink, !bLiveLinkEnabled);
	DGSetItemVisible(CurrentPalette->DialId, kDial_PauseLiveLink, bLiveLinkEnabled);
}

// Delete palette
void FPalette::Delete()
{
	if (CurrentPalette)
	{
		if (CurrentPalette->DialId)
		{
			DGModelessClose(CurrentPalette->DialId);
		}
		if (CurrentPalette)
		{
			UE_AC_DebugF("FPalette::Delete - Palette not deleted???\n");
		}
	}
}

// Constructor
FPalette::FPalette()
	: DialId(0)
	, LastItemHelp(0)
{
	UE_AC_Assert(CurrentPalette == nullptr);
	CurrentPalette = this;
	State = FPreferences::Get()->Prefs.Palette;

	DialId = DGCreateDockablePalette(ACAPI_GetOwnResModule(), LocalizeResId(kDlgPalette), ACAPI_GetOwnResModule(),
									 CntlDlgCallBack, (DGUserData)this, PaletteGuid);
	if (DialId != 0)
	{
		DGBeginProcessEvents(DialId);
		DGShowModelessDialog(DialId, DG_DF_FIRST);
#if 0
		short	HSize;
		short	VSize;
		DGGetDialogSize(LocalizeResId(kDlgPalette), DialId, DG_ORIGCLIENT, &HSize, &VSize);
	#if TRACE_PALETTE
        TraceF("FPalette::FPalette - GetDialogSize DG_ORIGCLIENT (%d, %d)\n", HSize, VSize);
	#endif
	#if 1
		DGSetDialogSize(DialId, DG_CLIENT, HSize, VSize, DG_TOPLEFT, false);
	#endif
#endif
		if (State.bIsDocked)
		{
			DGSetPaletteDockState(PaletteGuid, State.bIsDocked);
		}
		//        if (State.HiddenByAC || State.HiddenByUser)
		//        {
		//            DGHideModelessDialog(DialId);
		//        }
		SetPaletteMenuTexts(true, true);
		Save2Pref(); // save the dialog settings

		LiveLinkChanged();
	}
}

// Destructor
FPalette::~FPalette()
{
	if (DialId)
	{
		UE_AC_DebugF("FPalette::~FPalette - Destructor called with palette not closed???\n");
	}
	CurrentPalette = nullptr;
}

// Create Palette and set CurrentPalette
void FPalette::Create()
{
	if (CurrentPalette == nullptr)
	{
		CurrentPalette = new FPalette;
	}
}

// Save palette state to preferences
void FPalette::Save2Pref()
{
	if (DialId)
	{
		State.bIsDocked = DGIsPaletteDocked(PaletteGuid);
	}
	FPreferences* pref = FPreferences::Get();
	pref->Prefs.Palette = State;
	pref->Write();
}

// clang-format off
FAssValueName::SAssValueName Dg_Msg_Name[] = {
	ValueName(DG_MSG_NULL),
	ValueName(DG_MSG_INIT),
	ValueName(DG_MSG_CLOSEREQUEST),
	ValueName(DG_MSG_CLOSE),
	ValueName(DG_MSG_CLICK),
	ValueName(DG_MSG_DOUBLECLICK),
	ValueName(DG_MSG_CHANGE),
	ValueName(DG_MSG_TRACK),
	ValueName(DG_MSG_MOUSEMOVE),
	ValueName(DG_MSG_FOCUS),
	ValueName(DG_MSG_FILTERCHAR),
	ValueName(DG_MSG_HOTKEY),
	ValueName(DG_MSG_GROW),
	ValueName(DG_MSG_RESIZE),
	ValueName(DG_MSG_ACTIVATE),
	ValueName(DG_MSG_TOPSTATUSCHANGE),
	ValueName(DG_MSG_UPDATE),
	ValueName(DG_MSG_DRAGDROP),
	ValueName(DG_MSG_CONTEXTMENU),
	ValueName(DG_MSG_WHEELCLICK),
	ValueName(DG_MSG_WHEELTRACK),
	ValueName(DG_MSG_ITEMHELP),
	ValueName(DG_MSG_BACKGROUNDPAINT),
	ValueName(DG_MSG_LISTHEADERCLICK),
	ValueName(DG_MSG_LISTHEADERDRAG),
	ValueName(DG_MSG_LISTHEADERRESIZE),
	ValueName(DG_MSG_LISTHEADERBUTTONCLICK),
	ValueName(DG_MSG_SPLITTERDRAG),
	ValueName(DG_MSG_RESOLUTIONCHANGE),
	ValueName(DG_MSG_MOUSEDOWN),
	ValueName(DG_MSG_TREEITEMCLICK),
	ValueName(DG_MSG_TABBARITEMDRAG),
	ValueName(DG_MSG_SWITCHWND_BEGIN),
	ValueName(DG_MSG_SWITCHWND_NEXT),
	ValueName(DG_MSG_SWITCHWND_PREV),
	ValueName(DG_MSG_SWITCHWND_END),

	ValueName(DG_MSG_HOVER),
	ValueName(DG_MSG_PRESSED),
	ValueName(DG_MSG_UPDATEOVERLAY),
	ValueName(DG_MSG_CHANGEREQUEST),

	ValueName(DG_OF_MSG_FOLDERCHANGE),
	ValueName(DG_OF_MSG_SELCHANGE),
	ValueName(DG_OF_MSG_TYPECHANGE),

	EnumEnd(-1)};
// clang-format on

short FPalette::DlgCallBack(short Message, short DialID, short Item, DGMessageData MsgData)
{
#if TRACE_PALETTE
	bool bTrace = true;
	if (Message == DG_MSG_ITEMHELP || Message == DG_MSG_BACKGROUNDPAINT)
	{
		if (Item == LastItemHelp)
		{
			bTrace = false;
		} // One trace for repetive msg
		LastItemHelp = Item;
	}
	if (Message == DG_MSG_RESIZE)
	{
		bTrace = false;
	}
	if (bTrace)
	{
		TraceF("FPalette::DlgCallBack - %s item=%d\n", FAssValueName::GetName(Dg_Msg_Name, Message), Item);
	}
#endif
	(void)MsgData;
	switch (Message)
	{
		case DG_MSG_INIT:
			DGSetFocus(DialID, DG_NO_ITEM);
			break;

		case DG_MSG_ACTIVATE:
			break;

		case DG_MSG_RESIZE:
			{
#if TRACE_PALETTE
				DGResizeMsgData* ResizeMsgData = (DGResizeMsgData*)MsgData;
				TraceF("FPalette::DlgCallBack - DG_MSG_RESIZE (%d, %d)\n", ResizeMsgData->hGrow, ResizeMsgData->vGrow);
#endif
			}
			break;

		case DG_MSG_UPDATE:
			break;

		case DG_MSG_CHANGE:
			try
			{
				switch (Item)
				{
					case kDial_Snapshot:
						FCommander::DoSnapshot();
						break;
					case kDial_StartLiveLink:
					case kDial_PauseLiveLink:
						FCommander::ToggleLiveLink();
						break;
					case kDial_Connections:
						FCommander::ShowConnectionsDialog();
						break;
					case kDial_Export3D:
						FCommander::Export3DToFile();
						break;
					case kDial_Messages:
						FCommander::ShowMessagesDialog();
						break;
					case kDial_Information:
						FCommander::CopySelection2Clipboard();
						break;
					case kDial_ZapModelDB:
						FCommander::ZapDB();
						break;
					default:
						break;
				}
				DGSetItemValLong(DialID, Item, 0);
			}
			catch (...)
			{
				DGSetItemValLong(DialID, Item, 0);
				throw;
			}
			break;

		case DG_MSG_CLICK:
			switch (Item)
			{
				case DG_CLOSEBOX:
					State.bHiddenByUser = true; // Close requested by user
					Save2Pref(); // save the dialog settings
					SetPaletteMenuTexts(false, true);
					return Item; // this will result in a DG_MSG_CLOSE message
			}
			break;

		case DG_MSG_DOUBLECLICK:
			break;

		case DG_MSG_CLOSE:
			DialId = 0;
			delete this;
			break;
	}
	return 0;
}

void FPalette::SetPaletteMenuTexts(bool PaletteIsOn, bool PaletteIsVisible)
{
	GS::UniString ItemStr(GetGSName(PaletteIsOn ? kName_HidePalette : kName_ShowPalette));
	FMenus::SetMenuItemText(kStrListMenuItemPalette, 1, ItemStr);

	FMenus::SetMenuItemStatus(kStrListMenuItemPalette, 1, !PaletteIsVisible, API_MenuItemDisabled);
}

void FPalette::ShowHide(bool ByUserFromMenu, bool BeginHide)
{
	UE_AC_Assert(DialId != 0);
	UE_AC_Assert(DGIsDialogOpen(DialId));

	if (ByUserFromMenu)
	{
		if (DGIsModelessDialogVisible(DialId))
		{
			DGHideModelessDialog(DialId);
			SetPaletteMenuTexts(true, false);
			State.bHiddenByUser = true;
		}
		else if (!State.bHiddenByAC)
		{
			DGShowModelessDialog(DialId, DG_DF_FIRST);
			SetPaletteMenuTexts(true, true);
			State.bHiddenByUser = false;
		}
	}
	else
	{
		if (BeginHide)
		{
			State.bHiddenByAC = true;
			if (DGIsModelessDialogVisible(DialId))
			{
				DGHideModelessDialog(DialId);
				SetPaletteMenuTexts(true, false);
			}
		}
		else
		{
			State.bHiddenByAC = false;
			if (!State.bHiddenByUser)
			{
				DGShowModelessDialog(DialId, DG_DF_FIRST);
				SetPaletteMenuTexts(true, true);
			}
		}
	}
	Save2Pref();
}

GSErrCode FPalette::PaletteControlCallBack(Int32 ReferenceID, API_PaletteMessageID MessageID, GS::IntPtr Param)
{
	if (ReferenceID == DialId)
	{
		switch (MessageID)
		{
			case APIPalMsg_ClosePalette: // Called when quitting ArchiCAD
				DGModelessClose(DialId);
				// This is deleted when closing dialog
				break;

			case APIPalMsg_HidePalette_Begin: // Called a non 3D view is selected
			case APIPalMsg_HidePalette_End: // Called the 3d view is selected
				ShowHide(false, MessageID == APIPalMsg_HidePalette_Begin);
				break;

			case APIPalMsg_DisableItems_Begin: // Called when focus goes to another window or application
				/* if (!cntlDlgData.inMyInput)
				 EnablePaletteControls(false);
				 else
				 DisableInputControls();*/
				break;
			case APIPalMsg_DisableItems_End:
				/* EnablePaletteControls(true);*/
				break;
			case APIPalMsg_OpenPalette:
#if TRACE_PALETTE
				TraceF("FPalette::PaletteControlCallBack - APIPalMsg_OpenPalette while palette exist\n");
#endif
				break;
			case APIPalMsg_IsPaletteVisible:
				{
					bool bShowPalette = !State.bHiddenByUser;
#if !PALETTE_4_ALL_VIEW
					bShowPalette&& = Is3DCurrenWindow();
#endif
#if TRACE_PALETTE
					TraceF("FPalette::PaletteControlCallBack - APIPalMsg_IsPaletteVisible was=%d, new=%d\n",
						   *reinterpret_cast< bool* >(Param), bShowPalette);
#endif
					*reinterpret_cast< bool* >(Param) = bShowPalette;
				}
				break;
			default:
				break;
		}
	}
	return NoError;
}

template <>
FAssValueName::SAssValueName TAssEnumName< API_PaletteMessageID >::AssEnumName[] = {
	ValueName(APIPalMsg_ClosePalette),	   ValueName(APIPalMsg_HidePalette_Begin),
	ValueName(APIPalMsg_HidePalette_End),  ValueName(APIPalMsg_DisableItems_Begin),
	ValueName(APIPalMsg_DisableItems_End), ValueName(APIPalMsg_OpenPalette),
	ValueName(APIPalMsg_IsPaletteVisible), EnumEnd(-1)};

GSErrCode __ACENV_CALL FPalette::APIPaletteControlCallBack(Int32 ReferenceID, API_PaletteMessageID MessageID,
														   GS::IntPtr Param)
{
	GSErrCode GSErr = APIERR_GENERAL;
	try
	{
#if TRACE_PALETTE
		TraceF("FPalette::APIPaletteControlCallBack - Ref=%d, Msg=%s, param=%llu\n", ReferenceID,
			   TAssEnumName< API_PaletteMessageID >::GetName(MessageID), Param);
#endif
		if (ReferenceID == LocalizeResId(kDlgPalette))
		{
			if (CurrentPalette)
			{
				return CurrentPalette->PaletteControlCallBack(ReferenceID, MessageID, Param);
			}
			else
			{
				if (MessageID == APIPalMsg_IsPaletteVisible)
				{
#if PALETTE_4_ALL_VIEW
					bool bShowPalette = FPreferences::Get()->Prefs.Palette.bHiddenByUser;
#else
					bool bShowPalette = false;
#endif
#if TRACE_PALETTE
					TraceF("FPalette::APIPaletteControlCallBack - APIPalMsg_IsPaletteVisible was=%d, new=%d\n",
						   *reinterpret_cast< bool* >(Param), bShowPalette);
#endif
					*reinterpret_cast< bool* >(Param) = bShowPalette;
				}
				else if (MessageID == APIPalMsg_OpenPalette)
				{
					FPalette::ShowFromUser();
					if (CurrentPalette != nullptr)
					{
						// MainPalette->Show();
					}
				}
			}
		}
		GSErr = NoError;
	}
	catch (std::exception& e)
	{
		UE_AC_DebugF("FPalette::APIPaletteControlCallBack Ref(%d) Msg(%d) - Caught exception \"%s\"\n", ReferenceID,
					 MessageID, e.what());
	}
	catch (GS::GSException& gs)
	{
		UE_AC_DebugF("FPalette::APIPaletteControlCallBack Ref(%d) Msg(%d) - Caught exception \"%s\"\n", ReferenceID,
					 MessageID, gs.GetMessage().ToUtf8());
	}
	catch (...)
	{
		UE_AC_DebugF("FPalette::APIPaletteControlCallBack Ref(%d) Msg(%d) - Caught unknown exception\n", ReferenceID,
					 MessageID);
		ShowAlert("Unknown", "FPalette::APIPaletteControlCallBack");
	}

	return GSErr;
}

short DGCALLBACK FPalette::CntlDlgCallBack(short Message, short DialID, short Item, DGUserData userData,
										   DGMessageData MsgData)
{
	try
	{
		FPalette* palette = (FPalette*)userData;
		if (palette)
		{
			return palette->DlgCallBack(Message, DialID, Item, MsgData);
		}
		else
		{
			UE_AC_DebugF("FPalette::CntlDlgCallBack - palette is NULL\n");
		}
	}
	catch (UE_AC_Error& e)
	{
		ShowAlert(e, "FPalette::CntlDlgCallBack");
	}
	catch (std::exception& e)
	{
		ShowAlert(e.what(), "FPalette::CntlDlgCallBack");
	}
	catch (GS::GSException& gs)
	{
		ShowAlert(gs, "FPalette::CntlDlgCallBack");
	}
	catch (...)
	{
		ShowAlert("Unknown", "FPalette::CntlDlgCallBack");
	}
	return 0;
}

FPalette* FPalette::CurrentPalette = NULL;
GS::Guid  FPalette::PaletteGuid("245C6E1B-6BBA-4908-9890-3879C1E0CD5A");

END_NAMESPACE_UE_AC

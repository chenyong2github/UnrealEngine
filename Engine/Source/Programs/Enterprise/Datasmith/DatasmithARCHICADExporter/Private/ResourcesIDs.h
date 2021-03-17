// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// AddOn identifiers
#define kEpicGamesDevId 709308216
#define kDatasmithExporterId 2109425763

// Current supported languages
#define kLangUndefined 0
#define kLangEnglish 1
#define kLangFrench 2
#define kLangGerman 3
#define kLangSpanish 4
#define kLangItalian 5
#define kLangJapanese 6
#define kLangHungarian 7
#define kLangRussian 8
#define kLangGreec 9
#define kLangChinese 10
#define kLangPortuguese 11
#define kLangKorean 12

// Preprocessor macro to handle localizable resources
#define ResIdLocX(l, i) l##i
#define ResIdLoc(language, resid) ResIdLocX(language, resid)

#define ResIdLocAnchorX(l, i, a) l##i a##l
#define ResIdLocAnchor(language, resid, anchor) ResIdLocAnchorX(language, resid, anchor)

/* Resources (not language dependant) */
#define kAddOnIdentifier 32500

#define kPictureSplashScreen 32500

#define kIconSnapshot 32510
#define kIconConnections 32511
#define kIconExport3D 32512
#define kIconMessages 32513
#define kIconDS 32600

/* Localizable resources */
#define kStrLanguageName 300

#define kStrListSyncPlugInDescription 301
#define kStrListFileTypes 302

#define kStrListMenuDatasmith 310
#define kStrListMenuDatasmithHelp 410

#define kStrListMenuItemSnapshot 311
#define kStrListMenuItemSnapshotHelp 411
#define kStrListMenuItemConnections 312
#define kStrListMenuItemConnectionsHelp 412
#define kStrListMenuItemExport 313
#define kStrListMenuItemExportHelp 413
#define kStrListMenuItemMessages 314
#define kStrListMenuItemMessagesHelp 414
#define kStrListMenuItemPalette 315
#define kStrListMenuItemPaletteHelp 415
#define kStrListMenuItemAbout 316
#define kStrListMenuItemAboutHelp 416

#define kStrListProgression 330
#define kStrListENames 331

#define kDlgAboutOf 340
#define kDlgPalette 341

#define kAlertUnhandledError 350
#define kAlertSaveFileError 351
#define kAlertACDBError 352
#define kAlertPlugInError 353
#define kAlertNot3DViewError 354
#define kAlertUserCancelledError 355

#define LocalizeResId(id) short(UE_AC::GetCurrentLanguage() * 1000 + id)

#define UE_AC_STRINGIZE(text) UE_AC_STRINGIZE_A((text))
#define UE_AC_STRINGIZE_A(arg) UE_AC_STRINGIZE_I arg
#define UE_AC_STRINGIZE_I(text) #text

#ifndef RICKAD
	#define RICKAD 0
#endif

#if RIKCAD
	#define AC_RK_LR(l, r) AC_RK_TXT(l RIKCAD r)
	#define AC_RK_L(l) AC_RK_TXT(l RIKCAD)
	#define AC_RK_R(r) AC_RK_TXT(RIKCAD r)
#else
	#define AC_RK_LR(l, r) AC_RK_TXT(l ARCHICAD r)
	#define AC_RK_L(l) AC_RK_TXT(l ARCHICAD)
	#define AC_RK_R(r) AC_RK_TXT(ARCHICAD r)
#endif
#define AC_RK_TXT(text) #text

#define ADDON_SYNC_NAME AC_RK_LR(Datasmith, Exporter)
#define ADDON_MENU_TITLE "Datasmith"

#ifndef DEBUG
	#define kPaletteHSize 126
	#define kPaletteDevTools 0
#else
	#define kPaletteHSize 188
	#define kPaletteDevTools 1
#endif

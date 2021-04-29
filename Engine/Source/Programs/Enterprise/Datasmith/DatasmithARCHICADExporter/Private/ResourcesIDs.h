// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/ResourcesUtils.h"

// AddOn identifiers
#define kEpicGamesDevId 709308216
#define kDatasmithExporterId 2109425763

#define kPictureSplashScreen 32500

#define kIconSnapshot 32510
#define kIconConnections 32511
#define kIconExport3D 32512
#define kIconMessages 32513
#define kIconLiveSyncPlay 32514
#define kIconLiveSyncPause 32515
#define kIconDS 32600

/* Localizable resources */
#define kStrListFileTypes 302

#define kStrListMenuDatasmith 310
#define kStrListMenuDatasmithHelp 410

#define kStrListMenuItemSnapshot 311
#define kStrListMenuItemSnapshotHelp 411
#define kStrListMenuItemLiveLink 312
#define kStrListMenuItemLiveLinkHelp 412
#define kStrListMenuItemConnections 313
#define kStrListMenuItemConnectionsHelp 413
#define kStrListMenuItemExport 314
#define kStrListMenuItemExportHelp 414
#define kStrListMenuItemMessages 315
#define kStrListMenuItemMessagesHelp 415
#define kStrListMenuItemPalette 316
#define kStrListMenuItemPaletteHelp 416
#define kStrListMenuItemAbout 317
#define kStrListMenuItemAboutHelp 417

#define kStrListMenuItemPauseLiveLink 322
#define kStrListMenuItemPauseLiveLinkHelp 422

#define kStrListProgression 330

#define kDlgAboutOf 340
#define kDlgPalette 341
#define kDlgReport 342

#ifndef DEBUG
	#define kPaletteHSize 156
	#define kPaletteDevTools 0
#else
	#define kPaletteHSize 218
	#define kPaletteDevTools 1
#endif

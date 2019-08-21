// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VT/VirtualTextureBuildSettings.h"

static TAutoConsoleVariable<int32> CVarVTCompressCrunch(
	TEXT("r.VT.EnableCompressCrunch"),
	0,
	TEXT("Enable Crunch compression for virtual textures, for supported formats")
);

static TAutoConsoleVariable<int32> CVarVTCompressZlib(
	TEXT("r.VT.EnableCompressZlib"),
	1,
	TEXT("Enables Zlib compression for virtual textures, if no compression is enabled/supported")
);

static TAutoConsoleVariable<int32> CVarVTTileSize(
	TEXT("r.VT.TileSize"),
	128,
	TEXT("Size in pixels to use for virtual texture tiles (rounded to next power-of-2)")
);

static TAutoConsoleVariable<int32> CVarVTTileBorderSize(
	TEXT("r.VT.TileBorderSize"),
	4,
	TEXT("Size in pixels to use for virtual texture tiles borders (rounded to next power-of-2)")
);

void FVirtualTextureBuildSettings::Init()
{
	TileSize = CVarVTTileSize.GetValueOnAnyThread();
	TileBorderSize = CVarVTTileBorderSize.GetValueOnAnyThread();;
	bEnableCompressCrunch = CVarVTCompressCrunch.GetValueOnAnyThread() != 0;
	bEnableCompressZlib = CVarVTCompressZlib.GetValueOnAnyThread() != 0;
}

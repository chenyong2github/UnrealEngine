// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DatasmithMaxExporterDefines.h"

#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
#include "bitmap.h"
MAX_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"


// Helper structure to help manage loading and deleting bitmaps when we only have its BitmapInfo
struct FScopedBitMapPtr
{
public:
	BitmapInfo MapInfo;
	Bitmap* Map = nullptr;

	FScopedBitMapPtr(const BitmapInfo& InMapInfo, Bitmap* InMap);
	~FScopedBitMapPtr();

private:
	bool bNeedsDelete = false;
};

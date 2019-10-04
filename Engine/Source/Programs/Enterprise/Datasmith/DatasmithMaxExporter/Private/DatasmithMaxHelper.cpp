// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithMaxHelper.h"

FScopedBitMapPtr::FScopedBitMapPtr(const BitmapInfo& InMapInfo, Bitmap* InMap) : MapInfo(InMapInfo)
{
	if (InMap)
	{
		Map = InMap;
	}
	else
	{
		Map = TheManager->Load(&MapInfo);
		bNeedsDelete = Map != nullptr;
	}
}

FScopedBitMapPtr::~FScopedBitMapPtr()
{
	//If we load the bitmap it's our job to delete it as well.
	if (bNeedsDelete && Map)
	{
		Map->DeleteThis();
	}
}
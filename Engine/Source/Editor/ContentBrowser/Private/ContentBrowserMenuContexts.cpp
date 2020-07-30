// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContentBrowserMenuContexts.h"
#include "SContentBrowser.h"

FName UContentBrowserToolbarMenuContext::GetCurrentPath() const
{
	if (TSharedPtr<SContentBrowser> Browser = ContentBrowser.Pin())
	{
		return *Browser->GetCurrentPath();
	}

	return NAME_None;
}

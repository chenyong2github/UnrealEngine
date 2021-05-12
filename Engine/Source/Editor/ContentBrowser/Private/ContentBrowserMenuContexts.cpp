// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserMenuContexts.h"
#include "SContentBrowser.h"

FName UContentBrowserToolbarMenuContext::GetCurrentPath() const
{
	if (TSharedPtr<SContentBrowser> Browser = ContentBrowser.Pin())
	{
		return *Browser->GetCurrentPath(EContentBrowserPathType::Virtual);
	}

	return NAME_None;
}

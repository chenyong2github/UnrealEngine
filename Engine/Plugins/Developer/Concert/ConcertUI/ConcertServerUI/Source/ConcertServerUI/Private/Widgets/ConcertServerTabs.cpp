// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertServerTabs.h"

FName ConcertServerTabs::GetSessionBrowserTabId()
{
	static FName SessionBrowserTabId("SessionBrowserTabId");
	return SessionBrowserTabId;
}

FName ConcertServerTabs::GetOutputLogTabId()
{
	static FName OutputLogTabId("OutputLogTabId");
	return OutputLogTabId;
}
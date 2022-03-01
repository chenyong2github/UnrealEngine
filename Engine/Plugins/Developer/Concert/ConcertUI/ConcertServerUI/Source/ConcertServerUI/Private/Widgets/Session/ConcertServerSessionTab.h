// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IConcertServerSession;
class SDockTab;
class SWindow;

/** Manages the tab that contains the UI for a session */
class FConcertServerSessionTab
{
public:

	FConcertServerSessionTab(TSharedRef<IConcertServerSession> InspectedSession, const TSharedRef<SWindow>& ConstructUnderWindow);

	/** Opens or draws attention to the given tab */
	void OpenSessionTab() const;

private:

	/** The session being inspected */
	const TSharedRef<IConcertServerSession> InspectedSession;
	
	/** The tab containing the UI for InspectedSession */
	const TSharedRef<SDockTab> DockTab;

	/** Creates a tab widget */
	static TSharedRef<SDockTab> CreateTab(const TSharedRef<IConcertServerSession>& InspectedSession, const TSharedRef<SWindow>& ConstructUnderWindow);
	
	/** Generates a tab ID for FTabManager::InsertNewDocumentTab */
	static FString GetTabPlayerHolderId(const TSharedRef<IConcertServerSession>& InspectedSession);
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SConcertArchivedSessionInspector.h"

#include "Widgets/SessionTabs/AbstractConcertSessionTab.h"

/** Manages the tab for an archived session.  */
class FArchivedConcertSessionTab : public FAbstractConcertSessionTab
{
public:

	FArchivedConcertSessionTab(const FGuid& InspectedSessionID, TSharedRef<IConcertSyncServer> SyncServer, TAttribute<TSharedRef<SWindow>> ConstructUnderWindow);

protected:

	//~ Begin FAbstractConcertSessionTab Interface
	virtual FGuid GetSessionID() const override;
	virtual void CreateDockContent(const TSharedRef<SDockTab>& InDockTab) override;
	virtual void OnOpenTab() override;
	//~ End FAbstractConcertSessionTab Interface
	
private:

	/** The inspected session's ID */
	FGuid InspectedSessionID;

	/** Used later to obtain the window into which to add the tab */
	TAttribute<TSharedRef<SWindow>> ConstructUnderWindow;

	/** Displays session */
	TSharedPtr<SConcertArchivedSessionInspector> Inspector;
};

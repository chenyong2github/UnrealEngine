// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Window/IConcertComponent.h"

class FGlobalLogSource;
class SConcertClientsTabView;
class SWindow;

class FConcertClientsTabController : public IConcertComponent, public TSharedFromThis<FConcertClientsTabController>
{
public:
	
	FConcertClientsTabController();

	//~ Begin IConcertComponent Interface
	virtual void Init(const FConcertComponentInitParams& Params) override;
	//~ End IConcertComponent Interface

	/** Highlights this tab and sets the client filter such that all connected clients of the given session ID are shown.  */
	void ShowConnectedClients(const FGuid& SessionId) const;

private:

	/** Buffers generated logs up to a limit (and overrides oldest logs when buffer is full) */
	TSharedRef<FGlobalLogSource> LogBuffer;
	
	/** Manages the sub-tabs */
	TSharedPtr<SConcertClientsTabView> ClientsView;
	
	TSharedRef<SDockTab> SpawnClientsTab(const FSpawnTabArgs& Args, TSharedPtr<SWindow> RootWindow, TSharedRef<IConcertSyncServer>);
};

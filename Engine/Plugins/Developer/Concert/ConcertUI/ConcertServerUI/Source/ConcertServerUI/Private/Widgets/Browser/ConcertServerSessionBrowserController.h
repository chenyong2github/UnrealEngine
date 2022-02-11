// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Browser/IConcertServerSessionBrowserController.h"
#include "Widgets/IConcertComponent.h"

class FSpawnTabArgs;
class SDockTab;
class SConcertServerSessionBrowser;

/** Implements controller in model-view-controller pattern. */
class FConcertServerSessionBrowserController
	:
	public TSharedFromThis<FConcertServerSessionBrowserController>,
	public IConcertComponent,
	public IConcertServerSessionBrowserController
{
public:

	//~ Begin IConcertComponent Interface
	virtual void Init(const FConcertComponentInitParams& Params) override;
	//~ End IConcertComponent Interface

private:

	TSharedPtr<SConcertServerSessionBrowser> ConcertBrowser;
	
	TSharedRef<SDockTab> SpawnSessionBrowserTab(const FSpawnTabArgs& Args);
};

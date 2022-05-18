// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Window/IConcertComponent.h"

class SWindow;

class FConcertClientsTabController : public IConcertComponent
{
public:

	//~ Begin IConcertComponent Interface
	virtual void Init(const FConcertComponentInitParams& Params) override;
	//~ End IConcertComponent Interface

private:

	TSharedRef<SDockTab> SpawnClientsTab(const FSpawnTabArgs& Args, TSharedPtr<SWindow> RootWindow, TSharedRef<IConcertSyncServer>);
};

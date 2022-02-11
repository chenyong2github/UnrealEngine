// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/IConcertComponent.h"

class FSpawnTabArgs;
class SDockTab;

class FOutputLogController
	: public IConcertComponent
{
public:
	
	//~ Begin IConcertComponent Interface
	virtual void Init(const FConcertComponentInitParams& Params) override;
	//~ End IConcertComponent Interface

private:
	
	TSharedRef<SDockTab> SpawnOutputLogTab(const FSpawnTabArgs& Args);
};

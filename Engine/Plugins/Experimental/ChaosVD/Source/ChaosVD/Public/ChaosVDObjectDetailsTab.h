// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "ChaosVDTabSpawnerBase.h"
#include "Delegates/IDelegateInstance.h"

struct FChaosVDParticleDebugData;

class AActor;
class IDetailsView;
class FName;
class FSpawnTabArgs;
class FTabManager;
class SDockTab;


/** Spawns and handles and instance for the visual debugger details panel */
class FChaosVDObjectDetailsTab : public FChaosVDTabSpawnerBase, public TSharedFromThis<FChaosVDObjectDetailsTab>
{
public:

	FChaosVDObjectDetailsTab(const FName& InTabID, TSharedPtr<FTabManager> InTabManager, SChaosVDMainTab* InOwningTabWidget) : FChaosVDTabSpawnerBase(InTabID, InTabManager, InOwningTabWidget)
	{
	}

	virtual ~FChaosVDObjectDetailsTab() override;

protected:
	
	virtual TSharedRef<SDockTab> HandleTabSpawned(const FSpawnTabArgs& Args) override;
	
	void UpdateSelectedObject(AActor* NewObject) const;

	FDelegateHandle SelectionDelegateHandle;
	TSharedPtr<IDetailsView> DetailsPanel;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDTabSpawnerBase.h"

#include "ChaosVDEngine.h"
#include "ChaosVDScene.h"
#include "Widgets/SChaosVDMainTab.h"

FChaosVDTabSpawnerBase::FChaosVDTabSpawnerBase(const FName& InTabID, TSharedPtr<FTabManager> InTabManager, SChaosVDMainTab* InOwningTabWidget)
{
	OwningTabWidget = InOwningTabWidget;
	InTabManager->RegisterTabSpawner(InTabID, FOnSpawnTab::CreateRaw(this, &FChaosVDTabSpawnerBase::HandleTabSpawned));
}

UWorld* FChaosVDTabSpawnerBase::GetChaosVDWorld() const
{
	if (ensure(OwningTabWidget) && OwningTabWidget->GetChaosVDEngineInstance()->GetCurrentScene().IsValid())
	{
		return OwningTabWidget->GetChaosVDEngineInstance()->GetCurrentScene()->GetUnderlyingWorld();
	}

	return nullptr;
}

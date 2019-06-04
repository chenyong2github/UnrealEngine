// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/TabManager.h"

/** Interface for an Unreal Insights module. */
class IUnrealInsightsModule : public IModuleInterface
{
public:
	/**
	 * Called when application starts and a new layout is created. It allows the module to create its own areas.
	 *
	 * @param NewLayout The newly created layout.
	 */
	virtual void OnNewLayout(TSharedRef<FTabManager::FLayout> NewLayout) = 0;

	/**
	 * Called after application layout was restored.
	 *
	 * @param TabManager The tab manager used by Unreal Insights tools.
	 */
	virtual void OnLayoutRestored(TSharedPtr<FTabManager> TabManager) = 0;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/**
 * Interface for the Concert Sync UI module.
 */
class IConcertSyncUIModule : public IModuleInterface
{
public:
	/** Get the ConcertSyncUI module */
	static IConcertSyncUIModule& Get()
	{
		static const FName ModuleName = TEXT("ConcertSyncUI");
		return FModuleManager::Get().GetModuleChecked<IConcertSyncUIModule>(ModuleName);
	}
};

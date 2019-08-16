// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/** Module for virtual texturing editor extensions. */
class FVirtualTexturingEditorModule	: public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override;

private:
	void OnPlacementModeRefresh(FName CategoryName);
};

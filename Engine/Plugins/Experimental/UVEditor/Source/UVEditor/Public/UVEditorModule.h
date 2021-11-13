// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Besides the normal module things, the module class is also responsible for hooking the 
 * UV editor into existing menus.
 */
class UVEDITOR_API FUVEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	DECLARE_EVENT_OneParam(FUVEditorModule, FOnRegisterLayoutExtensions, FLayoutExtender&);
	virtual FOnRegisterLayoutExtensions& OnRegisterLayoutExtensions() { return RegisterLayoutExtensions; }

private:
	FOnRegisterLayoutExtensions	RegisterLayoutExtensions;

protected:
	void RegisterMenus();
};

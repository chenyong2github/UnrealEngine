// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleInterface.h"

#include "CoreMinimal.h"

class IAssetTypeActions;

DECLARE_LOG_CATEGORY_EXTERN(LogLensDistortionEditor, Log, All);


/**
 * Implements the LensDistortionEditor module.
 */
class FLensDistortionEditorModule : public IModuleInterface 
{
public:

	//~ Begin IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface

private:

	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
};

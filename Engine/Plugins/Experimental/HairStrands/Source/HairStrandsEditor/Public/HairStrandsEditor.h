// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/** Implements the HairStrands module  */
class FHairStrandsEditor : public IModuleInterface
{
public:

	//~ IModuleInterface interface

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}

private:

	/** The collection of registered asset type actions. */
	TArray<TSharedRef<class IAssetTypeActions>> RegisteredAssetTypeActions;
};

IMPLEMENT_MODULE(FHairStrandsEditor, HairStrandsEditor);

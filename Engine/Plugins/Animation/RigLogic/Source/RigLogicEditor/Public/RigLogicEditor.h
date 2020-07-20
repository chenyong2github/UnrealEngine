// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

struct RIGLOGICEDITOR_API FRigLogicEditor : IModuleInterface 
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	FRigLogicEditor();

private:
	TArray<TSharedRef<class IAssetTypeActions>> AssetTypeActions;
};

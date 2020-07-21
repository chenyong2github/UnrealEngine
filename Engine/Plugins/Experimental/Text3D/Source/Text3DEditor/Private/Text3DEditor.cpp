// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"


class FText3DEditorModule final : public IModuleInterface
{
public:
	FText3DEditorModule() = default;

	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};


IMPLEMENT_MODULE(FText3DEditorModule, Text3DEditor)

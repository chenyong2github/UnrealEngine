// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IWebAPIBlueprintGraphModule.h"

#include "CoreMinimal.h"

class FWebAPIBlueprintGraphModule final
    : public IWebAPIBlueprintGraphModuleInterface
{
public:
    void StartupModule() override;
    void ShutdownModule() override;
};

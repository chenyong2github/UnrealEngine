// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IWebAPIModule.h"

#include "CoreMinimal.h"

class FWebAPIModule final
    : public IWebApiModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};

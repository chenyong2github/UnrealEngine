// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "DatasmithIFCImportOptions.generated.h"

UCLASS(config = EditorPerProjectUserSettings, HideCategories = (DebugProperty))
class UDatasmithIFCImportOptions : public UObject
{
	GENERATED_BODY()

	UDatasmithIFCImportOptions();

public:
};

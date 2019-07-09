// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "LevelEditorMenuContext.generated.h"

class SLevelEditor;

UCLASS()
class ULevelEditorMenuContext : public UObject
{
	GENERATED_BODY()
public:
	TWeakPtr<SLevelEditor> SlateLevelEditor;
};

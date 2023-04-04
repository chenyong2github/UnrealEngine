// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "InstancedStruct.h"
#include "IChooserParameterBase.h"
#include "IChooserParameterRandomize.generated.h"

struct FRandomizationState
{
	int LastSelectedRow;
};

USTRUCT(BlueprintType)
struct FChooserRandomizationContext
{
	GENERATED_BODY();

	TMap<const void*, FRandomizationState> StateMap;
};

USTRUCT()
struct FChooserParameterRandomizeBase : public FChooserParameterBase
{
	GENERATED_BODY()
	virtual bool GetValue(const UObject* ContextObject, const FChooserRandomizationContext*& OutResult) const { return false; }
};

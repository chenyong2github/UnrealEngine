// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "IChooserParameterProxyTable.generated.h"

class UProxyTable;

UINTERFACE(NotBlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class PROXYTABLE_API UChooserParameterProxyTable : public UInterface
{
	GENERATED_BODY()
};

class PROXYTABLE_API IChooserParameterProxyTable
{
	GENERATED_BODY()

public:
	virtual bool GetValue(const UObject* ContextObject, const UProxyTable*& OutResult) const { return false; }
};
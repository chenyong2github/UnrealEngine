// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "IChooserColumn.generated.h"

UINTERFACE(NotBlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class CHOOSER_API UChooserColumn : public UInterface
{
	GENERATED_BODY()
};

class CHOOSER_API IChooserColumn 
{
	GENERATED_BODY()

public:
	virtual void Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) {};
	virtual void SetNumRows(uint32 NumRows) {}
	virtual void DeleteRows(const TArray<uint32> & RowIndices) {}
	virtual UClass* GetInputValueInterface() {return nullptr;};
	virtual UObject* GetInputValue() {return nullptr;}
	virtual void SetInputValue(UObject* InputValue) {}
};

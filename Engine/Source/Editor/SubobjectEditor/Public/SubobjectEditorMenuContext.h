// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SubobjectEditorMenuContext.generated.h"

class SSubobjectEditor;

UCLASS()
class SUBOBJECTEDITOR_API USubobjectEditorMenuContext : public UObject
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category="Tool Menus")
	TArray<UObject*> GetSelectedObjects() const;

	TWeakPtr<SSubobjectEditor> SubobjectEditor;
	
	bool bOnlyShowPasteOption;
};

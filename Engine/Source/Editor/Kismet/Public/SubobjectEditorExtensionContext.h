// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SubobjectEditorExtensionContext.generated.h"

class SSubobjectEditor;
class SSubobjectInstanceEditor;
class SSubobjectBlueprintEditor;

UCLASS()
class KISMET_API USubobjectEditorExtensionContext : public UObject
{
	GENERATED_BODY()

public:
	
	const TWeakPtr<SSubobjectEditor>& GetSubobjectEditor() const { return SubobjectEditor; }

private:
	friend SSubobjectEditor;
	friend SSubobjectInstanceEditor;
	friend SSubobjectBlueprintEditor;

	TWeakPtr<SSubobjectEditor> SubobjectEditor;
};

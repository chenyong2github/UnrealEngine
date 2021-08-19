// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SubobjectEditorMenuContext.generated.h"

class SSubobjectEditor;

UCLASS()
class KISMET_API USubobjectEditorMenuContext : public UObject
{
	GENERATED_BODY()
public:

	TWeakPtr<SSubobjectEditor> SubobjectEditor;
	
	bool bOnlyShowPasteOption;
};

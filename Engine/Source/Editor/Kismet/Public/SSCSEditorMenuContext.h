// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SSCSEditorMenuContext.generated.h"

class SSCSEditor;
class SSubobjectEditor;

// #TODO_BH Rename this for subobject editor menu context
UCLASS()
class KISMET_API USSCSEditorMenuContext : public UObject
{
	GENERATED_BODY()
public:

	TWeakPtr<SSCSEditor> SCSEditor;

	TWeakPtr<SSubobjectEditor> SubobjectEditor;

	bool bOnlyShowPasteOption;
};

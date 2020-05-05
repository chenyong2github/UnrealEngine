// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SSCSEditorMenuContext.generated.h"

class SSCSEditor;

UCLASS()
class KISMET_API USSCSEditorMenuContext : public UObject
{
	GENERATED_BODY()
public:
	
	TWeakPtr<SSCSEditor> SCSEditor;

	bool bOnlyShowPasteOption;
};

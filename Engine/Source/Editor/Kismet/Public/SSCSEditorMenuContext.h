// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SSCSEditorMenuContext.generated.h"

class SSCSEditor;

UCLASS()
class USSCSEditorMenuContext : public UObject
{
	GENERATED_BODY()
public:
	
	TWeakPtr<SSCSEditor> SCSEditor;

	bool bOnlyShowPasteOption;
};

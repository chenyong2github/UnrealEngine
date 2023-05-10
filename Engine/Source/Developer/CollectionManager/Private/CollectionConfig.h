// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "CollectionConfig.generated.h"

#pragma once

UCLASS(minimalapi, config=EditorSettings, meta=(DisplayName = "Collections", ToolTip="Settings to tweak the behaviour of collections in the editor"))
class UCollectionConfig : public UObject
{
	GENERATED_BODY()
public:
	/** When enabled, Shared and Private collections will automatically commit their changes to source control */
	UPROPERTY(config)
	bool bAutoCommitOnSave = true;
};
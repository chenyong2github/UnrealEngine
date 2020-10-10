// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "UObject/WeakObjectPtr.h"

#include "DisplayClusterConfiguratorEditorSubsystem.generated.h"

class UDisplayClusterConfiguratorEditorData;

UCLASS()
class UDisplayClusterConfiguratorEditorSubsystem 
	: public UEditorSubsystem
{
	GENERATED_BODY()

public:
	bool ReimportAsset(UDisplayClusterConfiguratorEditorData* InConfiguratorEditorData);

	bool ReloadConfig(UDisplayClusterConfiguratorEditorData* InConfiguratorEditorData, const FString& InConfigPath);

	bool RenameAssets(const TWeakObjectPtr<UObject>& InAsset, const FString& InNewPackagePath, const FString& InNewName);

	bool SaveConfig(UDisplayClusterConfiguratorEditorData* InConfiguratorEditorData, const FString& InConfigPath);
};

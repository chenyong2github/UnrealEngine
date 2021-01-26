// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorConfig.h"
#include "EditorSubsystem.h"

#include "EditorConfigSubsystem.generated.h"

DECLARE_DELEGATE_OneParam(FOnCompletedDelegate, bool);

UCLASS()
class EDITORCONFIG_API UEditorConfigSubsystem : 
	public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UEditorConfigSubsystem();

	void Initialize(FSubsystemCollectionBase& Collection);

	TSharedPtr<FEditorConfig> FindOrLoadConfig(FStringView ConfigName);
	void SaveConfig(TSharedPtr<FEditorConfig> Config, FOnCompletedDelegate OnCompleted);

	void AddSearchDirectory(FStringView SearchDir);

private:
	void OnSaveCompleted(TSharedPtr<FEditorConfig> Config);

private:
	struct FPendingSave
	{
		FString FileName;
		TSharedPtr<FEditorConfig> Config;
		TFuture<bool> WasSuccess;
		FOnCompletedDelegate OnCompleted;
	};

	FRWLock SaveLock;
	TArray<FPendingSave> PendingSaves;
	TArray<FString> SearchDirectories;
	TMap<FString, TSharedPtr<FEditorConfig>> LoadedConfigs;
};

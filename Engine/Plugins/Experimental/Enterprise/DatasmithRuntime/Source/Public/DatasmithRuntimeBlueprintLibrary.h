// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Kismet/BlueprintFunctionLibrary.h"

#include "DatasmithRuntimeBlueprintLibrary.generated.h"

class ADatasmithRuntimeActor;
class UWorld;

UCLASS(meta = (ScriptName = "DatasmithRuntimeLibrary"))
class DATASMITHRUNTIME_API UDatasmithRuntimeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a DatasmithRuntime actor
	 * @param TargetWorld	The world in which to create the DatasmithRuntime actor.
	 * @param RootTM		The transform to apply to the DatasmithRuntime actor on creation.
	 * @return	A pointer to the DatasmithRuntime actor if the createion is successful; nullptr, otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	static bool LoadDatasmithScene(ADatasmithRuntimeActor* DatasmithRuntimeActor, const FString& FilePath);

	/*
	 * Opens a file dialog for the specified data. Leave FileTypes empty to be able to select any files.
	 * Filetypes must be in the format of: <File type Description>|*.<actual extension>
	 * You can combine multiple extensions by placing ";" between them
	 * For example: Text Files|*.txt|Excel files|*.csv|Image Files|*.png;*.jpg;*.bmp will display 3 lines for 3 different type of files.
	*/
	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	static void LoadDatasmithSceneFromExplorer(ADatasmithRuntimeActor* DatasmithRuntimeActor, const FString& DefaultPath, const FString& FileTypes);
	
	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	static void ResetActor(ADatasmithRuntimeActor* DatasmithRuntimeActor);
};

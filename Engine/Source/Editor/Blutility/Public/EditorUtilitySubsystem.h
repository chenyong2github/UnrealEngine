// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"

#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "EditorUtilitySubsystem.generated.h"

class SWindow;
class UEditorUtilityWidget;

UCLASS(config = EditorPerProjectUserSettings)
class BLUTILITY_API UEditorUtilitySubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UEditorUtilitySubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection);
	virtual void Deinitialize();

	void MainFrameCreationFinished(TSharedPtr<SWindow> InRootWindow, bool bIsNewProjectWindow);
	void HandleStartup();

	UPROPERTY(config)
	TArray<FSoftObjectPath> LoadedUIs;

	UPROPERTY(config)
	TArray<FSoftObjectPath> StartupObjects;

	// Allow startup object to be garbage collected
	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	void ReleaseInstanceOfAsset(UObject* Asset);

	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	bool TryRun(UObject* Asset);

	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	UEditorUtilityWidget* SpawnAndRegisterTabAndGetID(class UEditorUtilityWidgetBlueprint* InBlueprint, FName& NewTabID);

	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	UEditorUtilityWidget* SpawnAndRegisterTab(class UEditorUtilityWidgetBlueprint* InBlueprint);

	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	void RegisterTabAndGetID(class UEditorUtilityWidgetBlueprint* InBlueprint, FName& NewTabID);

	/** Given an ID for a tab, try to find a tab spawner that matches, and then spawn a tab. Returns true if it was able to find a matching tab spawner */
	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	bool SpawnRegisteredTabByID(FName NewTabID);

	/** Given an ID for a tab, try to find an existing tab. Returns true if it found a tab. */
	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	bool DoesTabExist(FName NewTabID);

	/** Given an ID for a tab, try to find and close an existing tab. Returns true if it found a tab to close. */
	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	bool CloseTabByID(FName NewTabID);

	/** Given an editor utility widget blueprint, get the widget it creates. This will return a null pointer if the widget is not currently in a tab.*/
	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	UEditorUtilityWidget* FindUtilityWidgetFromBlueprint(class UEditorUtilityWidgetBlueprint* InBlueprint);

private:
	
	UPROPERTY()
	TMap<UObject*, UObject*> ObjectInstances;
};

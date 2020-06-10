// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "EditorActorUtilities.generated.h"

UCLASS()
class UNREALED_API UEditorActorUtilities : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Duplicate all the selected actors in the given world
	 * @param	InWorld 	The world the actors are selected in.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Level Utility")
		static void DuplicateSelectedActors(UWorld* InWorld);

	/**
	 * Delete all the selected actors in the given world
	 * @param	InWorld 	The world the actors are selected in.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Level Utility")
		static void DeleteSelectedActors(UWorld* InWorld);

	/**
	 * Invert the selection in the given world
	 * @param	InWorld 	The world the selection is in.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Level Utility")
		static void InvertSelection(UWorld* InWorld);

	/**
	* Select all actors and BSP models in the given world, except those which are hidden
	*  @param	InWorld 	The world the actors are to be selected in.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Level Utility")
		static void SelectAll(UWorld* InWorld);

	/**
	 * Select all children actors of the current selection.
	 *
	 * @param   bRecurseChildren	True to recurse through all descendants of the children
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Level Utility")
		static void SelectAllChildren(bool bRecurseChildren);
};

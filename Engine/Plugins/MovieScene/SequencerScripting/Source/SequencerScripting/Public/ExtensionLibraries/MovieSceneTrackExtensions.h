// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "MovieSceneTrackExtensions.generated.h"

class FText;

class UMovieSceneTrack;
class UMovieSceneSection;

/**
 * Function library containing methods that should be hoisted onto UMovieSceneTracks for scripting
 */
UCLASS()
class UMovieSceneTrackExtensions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Set this track's display name
	 *
	 * @param Track        The track to use
	 * @param InName The name for this track
	 */
	UFUNCTION(BlueprintCallable, Category="Track", meta=(ScriptMethod))
	static void SetDisplayName(UMovieSceneTrack* Track, const FText& InName);

	/**
	 * Get this track's display name
	 *
	 * @param Track        The track to use
	 * @return This track's display name
	 */
	UFUNCTION(BlueprintCallable, Category="Track", meta=(ScriptMethod))
	static FText GetDisplayName(UMovieSceneTrack* Track);

	/**
	 * Add a new section to this track
	 *
	 * @param Track        The track to use
	 * @return The newly create section if successful
	 */
	UFUNCTION(BlueprintCallable, Category="Track", meta=(ScriptMethod))
	static UMovieSceneSection* AddSection(UMovieSceneTrack* Track);

	/**
	 * Access all this track's sections
	 *
	 * @param Track        The track to use
	 * @return An array of this track's sections
	 */
	UFUNCTION(BlueprintCallable, Category="Track", meta=(ScriptMethod))
	static TArray<UMovieSceneSection*> GetSections(UMovieSceneTrack* Track);

	/**
	 * Remove the specified section
	 *
	 * @param Track        The track to remove the section from, if present
	 * @param Section      The section to remove
	 */
	UFUNCTION(BlueprintCallable, Category="Track", meta=(ScriptMethod))
	static void RemoveSection(UMovieSceneTrack* Track, UMovieSceneSection* Section);

	/**
	 * Get the sorting order for this track
	 *
	 * @param Track        The track to get the sorting order from
	 * @return The sorting order of the requested track
	 */
	UFUNCTION(BlueprintCallable, Category="Track", meta=(ScriptMethod))
	static int32 GetSortingOrder(UMovieSceneTrack* Track);
 
	/**
	 * Set the sorting order for this track
	 *
	 * @param Track        The track to get the sorting order from
	 * @param SortingOrder The sorting order to set
	 */
	UFUNCTION(BlueprintCallable, Category="Track", meta=(ScriptMethod))
	static void SetSortingOrder(UMovieSceneTrack* Track, int32 SortingOrder);
 
	/**
	 * Get the color tint for this track
	 *
	 * @param Track        The track to get the color tint from
	 * @return The color tint of the requested track
	 */
	UFUNCTION(BlueprintCallable, Category="Track", meta=(ScriptMethod))
	static FColor GetColorTint(UMovieSceneTrack* Track);
 
	/**
	 * Set the color tint for this track
	 *
	 * @param Track        The track to get the color tint from
	 * @param ColorTint The color tint to set
	 */
	UFUNCTION(BlueprintCallable, Category="Track", meta=(ScriptMethod))
	static void SetColorTint(UMovieSceneTrack* Track, const FColor& ColorTint);
 };
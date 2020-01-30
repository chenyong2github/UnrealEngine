// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapPlanesTypes.h"
#include "MagicLeapPlanesFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPPLANES_API UMagicLeapPlanesFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Create a planes tracker. */
	UFUNCTION(BlueprintCallable, Category = "Planes Function Library | MagicLeap")
	static bool CreateTracker();

	/** Destroy a planes tracker. */
	UFUNCTION(BlueprintCallable, Category = "Planes Function Library | MagicLeap")
	static bool DestroyTracker();

	/** Is a planes tracker already created. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Planes Function Library | MagicLeap")
	static bool IsTrackerValid();

	/** Initiates a plane query. */
	UFUNCTION(BlueprintCallable, Category = "Planes Function Library | MagicLeap")
	static bool PlanesQueryBeginAsync(const FMagicLeapPlanesQuery& Query, const FMagicLeapPlanesResultDelegate& ResultDelegate);

	/**
		Gets the expected scale of the actor to be placed within the bounds of the given plane.
		Ensure that the actor rotation is 0 (FQuat::Identity) before sending it to this function.
		@param ContentActor Actor for which the scale is to be calculated.
		@param PlaneDimensions Dimensions (in Unreal units) of the plane the actor has to be placed on.
		@return A vector representing the expected scale of the Actor.
	*/
	UFUNCTION(BlueprintCallable, Category = "Planes Function Library | MagicLeap")
	static FTransform GetContentScale(const AActor* ContentActor, const FMagicLeapPlaneResult& PlaneResult);

	/**
		Re-orders an array of plane query flags based on the priority list passed.
		@param InPriority The priority list by which to order the array of plane query flags.
		@param InFlagsToReorder The array of plane query flags to be reordered.
		@param OutReorderedFlags The reordered array of plane query flags.
	*/
	UFUNCTION(BlueprintCallable, Category = "Planes Function Library | MagicLeap")
	static void ReorderPlaneFlags(const TArray<EMagicLeapPlaneQueryFlags>& InPriority, const TArray<EMagicLeapPlaneQueryFlags>& InFlagsToReorder, TArray<EMagicLeapPlaneQueryFlags>& OutReorderedFlags);

	/**
		Removes 
		@param InPriority The priority list by which to order the array of plane query flags.
		@param InFlagsToReorder The array of plane query flags to be reordered.
		@param OutReorderedFlags The reordered array of plane query flags.
	*/
	UFUNCTION(BlueprintCallable, Category = "Planes Function Library | MagicLeap")
	static void RemoveFlagsNotInQuery(const TArray<EMagicLeapPlaneQueryFlags>& InQueryFlags, const TArray<EMagicLeapPlaneQueryFlags>& InResultFlags, TArray<EMagicLeapPlaneQueryFlags>& OutFlags);
};

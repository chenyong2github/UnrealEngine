// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"

#include "Containers/Map.h"
#include "Containers/UnrealString.h"

#include "CalibrationPointComponent.generated.h"

/**
 * One or more instances of this component can be added to an actor (e.g. a static mesh actor blueprint), 
 * and should be placed at geometrically and visually distinct landmarks of the object.
 * These 3d points will then be optionally used by any given nodal offset tool implementation to
 * make a 3d-2d correspondence with the 2d points detected in the live action media.
 */
UCLASS(ClassGroup = (Calibration), meta = (BlueprintSpawnableComponent), meta = (DisplayName = "Calibration Point"))
class CAMERACALIBRATIONCORE_API UCalibrationPointComponent : public USceneComponent
{
	GENERATED_BODY() 

public:

	/** 
	 * A way to group many points in a single component. 
	 */
	UPROPERTY(EditAnywhere, Category="Calibration")
	TMap<FString,FVector> SubPoints;

	/** Optional pointer to mesh that the point(s) belong to */
	UPROPERTY(EditInstanceOnly, Category = "Calibration", meta = (UseComponentPicker, AllowedClasses = "StaticMeshComponent", DisallowedClasses = "CalibrationPointComponent"))
	FComponentReference Mesh;

	/** 
	 * Returns the World location of the subpoint (or the component) specified by name 
	 * 
	 * @param InPointName Name of the point or subpoint. If not namespaced the component name wil have priority over subpoint name.
	 * @param OutLocation World location of the specified subpoint.
	 * 
	 * @return True if successful.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Calibration")
	bool GetWorldLocation(const FString& InPointName, FVector& OutLocation) const;

	/**
	 * Namespaces the given subpoint name. Does not check that the subpoint exists.
	 * 
	 * @param InSubpointName Name of the subpoint to namespace
	 * @param OutNamespacedName The output namespaced subpoint name
	 * 
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Calibration")
	bool NamespacedSubpointName(const FString& InSubpointName, FString& OutNamespacedName) const;

	/** 
	 * Gathers the namespaced names of the subpoints and the component itself.
	 * 
	 * @param OutNamespacedNames Array of names to be filled out by this function. Will not empty it.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Calibration")
	void GetNamespacedPointNames(TArray<FString>& OutNamespacedNames) const;
};

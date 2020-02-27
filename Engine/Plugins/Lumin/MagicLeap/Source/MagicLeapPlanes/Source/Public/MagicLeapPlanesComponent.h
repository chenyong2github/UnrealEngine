// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Components/ActorComponent.h"
#include "MagicLeapPlanesTypes.h"
#include "MagicLeapPlanesComponent.generated.h"

/**
	Component that provides access to the Planes API functionality.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPPLANES_API UMagicLeapPlanesComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMagicLeapPlanesComponent();

	/** Creates the planes tracker handle for the component (or adds a ref count to an existing one). */
	virtual void BeginPlay() override;

	/** Removes a ref count to the underlying tracker (destroys the tracker if this is the last user). */
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Initiates a plane query. */
	UFUNCTION(BlueprintCallable, Category = "Planes | MagicLeap")
	bool RequestPlanesAsync();

	/** The flags to apply to this query. TODO: Should be a TSet but that is misbehaving in the editor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes | MagicLeap")
	TArray<EMagicLeapPlaneQueryFlags> QueryFlags;

	/** Bounding box for searching planes in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes | MagicLeap")
	class UBoxComponent* SearchVolume;

	/** The maximum number of planes that should be returned in the result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes | MagicLeap", meta = (ClampMin = 0))
	int32 MaxResults;

	/**
	  If EMagicLeapPlaneQueryFlags::IgnoreHoles is not a query flag then holes with a perimeter (in Unreal Units)
	  smaller than this value will be ignored, and can be part of the plane.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes | MagicLeap", meta = (ClampMin = 0))
	float MinHolePerimeter;

	/**
	  The minimum area (in squared Unreal Units) of planes to be returned.
	  This value cannot be lower than 400 (lower values will be capped to this minimum).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes | MagicLeap", meta = (ClampMin = 400))
	float MinPlaneArea;

	/**
	  The type of plane query to perform.
	  Bulk: Use OnPlanesQueryResult to retrieve results.
	  Delta: Use OnPersistentPlanesQueryResult to retrieve results.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes | MagicLeap")
	EMagicLeapPlaneQueryType QueryType;

	/**
		The threshold used to compare incoming planes with any cached planes.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planes | MagicLeap", meta = (ClampMin = 0.0))
	float SimilarityThreshold;
	
 private:
	// Delegate instances
	UPROPERTY(BlueprintAssignable, Category = "Planes | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapPlanesResultDelegateMulti OnPlanesQueryResult;

	UPROPERTY(BlueprintAssignable, Category = "Planes | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapPersistentPlanesResultDelegateMulti OnPersistentPlanesQueryResult;
	
	EMagicLeapPlaneQueryType CurrentQueryType;
	FGuid QueryHandle;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "MRMeshComponent.h"
#include "MagicLeapHandMeshingTypes.h"
#include "MagicLeapHandMeshingComponent.generated.h"

/**
	Component that provides access to the HandMeshing API functionality.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPHANDMESHING_API UMagicLeapHandMeshingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
		Destroys the hand meshing client.
	*/
	void EndPlay(const EEndPlayReason::Type EndPlayReason);

	/**
		Connects the MRMesh component.
		@param InMRMeshPtr The MRMeshComponent to be used as the hand meshing source.
		@return True if successful, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "HandMeshing Function Library | MagicLeap")
	bool ConnectMRMesh(UMRMeshComponent* InMRMeshPtr);

	/**
		Disconnects the MRMesh component.
		@param InMRMeshPtr The MRMeshComponent to be removed as the hand meshing source.
		@return True if successful, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "HandMeshing Function Library | MagicLeap")
	bool DisconnectMRMesh(class UMRMeshComponent* InMRMeshPtr);
};

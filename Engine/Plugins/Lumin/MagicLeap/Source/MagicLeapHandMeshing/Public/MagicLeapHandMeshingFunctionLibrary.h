// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapHandMeshingTypes.h"
#include "MagicLeapHandMeshingFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPHANDMESHING_API UMagicLeapHandMeshingFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
		Create the hand meshing client Note that this will be the only function in the HandMeshing API that will raise a
		PrivilegeDenied error.  Trying to call the other functions with an invalid MLHandle will result in an InvalidParam error.
		@return True if successful, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "HandMeshing Function Library | MagicLeap")
	static bool CreateClient();

	/** 
		Destroys the hand meshing client.
		@return True if successful, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "HandMeshing Function Library | MagicLeap")
	static bool DestroyClient();

	/** 
		Connects the MRMesh component.
		@param InMRMeshPtr The MRMeshComponent to be used as the hand meshing source.
		@return True if successful, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "HandMeshing Function Library | MagicLeap")
	static bool ConnectMRMesh(UMRMeshComponent* InMRMeshPtr);

	/** 
		Disconnects the MRMesh component.
		@param InMRMeshPtr The MRMeshComponent to be removed as the hand meshing source.
		@return True if successful, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "HandMeshing Function Library | MagicLeap")
	static bool DisconnectMRMesh(class UMRMeshComponent* InMRMeshPtr);
};

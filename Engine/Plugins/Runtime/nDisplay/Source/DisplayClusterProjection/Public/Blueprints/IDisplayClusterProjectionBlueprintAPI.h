// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "IDisplayClusterProjectionBlueprintAPI.generated.h"

class UCameraComponent;


UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class DISPLAYCLUSTERPROJECTION_API UDisplayClusterProjectionBlueprintAPI : public UInterface
{
	GENERATED_BODY()
};


/**
 * Blueprint API interface
 */
class DISPLAYCLUSTERPROJECTION_API IDisplayClusterProjectionBlueprintAPI
{
	GENERATED_BODY()

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Policy: CAMERA
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Sets active camera component for camera policy */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set camera"), Category = "NDisplayProjection|Camera")
	virtual void CameraPolicySetCamera(const FString& ViewportId, UCameraComponent* NewCamera, float FOVMultiplier = 1.f) = 0;

	/** Create link to static mesh geometry as warp source */
	//UFUNCTION(BlueprintCallable, meta = (DisplayName = "Assign Warp Mesh To Viewport"), Category = "NDisplayProjection|Mesh")
	UE_DEPRECATED(4.26, "use config reference")
	virtual void AssignWarpMeshToViewport(const FString& ViewportId, UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent)
	{ }
};

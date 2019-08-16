// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "AppleARKitVideoOverlay.generated.h"

class UARTextureCameraImage;
class UMaterialInstanceDynamic;

/** Helper class to ensure the ARKit camera material is cooked. */
UCLASS()
class UARKitCameraOverlayMaterialLoader : public UObject
{
	GENERATED_BODY()
	
public:
	UPROPERTY()
	UMaterialInterface* DefaultCameraOverlayMaterial;

	UARKitCameraOverlayMaterialLoader()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultOverlayMaterialRef(TEXT("/AppleARKit/M_CameraOverlay.M_CameraOverlay"));
		DefaultCameraOverlayMaterial = DefaultOverlayMaterialRef.Object;
	}
};

class FAppleARKitVideoOverlay
	: public FGCObject
{
public:
	FAppleARKitVideoOverlay();

	void SetCameraTexture(UARTextureCameraImage* InCameraImage);

	void RenderVideoOverlay_RenderThread(FRHICommandListImmediate& RHICmdList, const FSceneView& InView, struct FAppleARKitFrame& Frame, const EDeviceScreenOrientation DeviceOrientation);
	bool GetPassthroughCameraUVs_RenderThread(TArray<FVector2D>& OutUVs, const EDeviceScreenOrientation DeviceOrientation);

	void SetOverlayTexture(UARTextureCameraImage* InCameraImage);

private:
	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//~ FGCObject

	UMaterialInstanceDynamic* MID_CameraOverlay;
	UMaterialInterface* RenderingOverlayMaterial;

	// Cache UV offset to be used by GetPassthroughCameraUVs_RenderThread
	FVector2D UVOffset;

	FVertexBufferRHIRef OverlayVertexBufferRHI[2];
	FIndexBufferRHIRef IndexBufferRHI;
};

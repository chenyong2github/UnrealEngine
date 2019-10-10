// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "AppleARKitAvailability.h"

#if SUPPORTS_ARKIT_1_0
	#import <ARKit/ARKit.h>
#endif

#include "AppleARKitVideoOverlay.generated.h"

class UARTextureCameraImage;
class UMaterialInstanceDynamic;
class UAppleARKitOcclusionTexture;

/** Helper class to ensure the ARKit camera material is cooked. */
UCLASS()
class UARKitCameraOverlayMaterialLoader : public UObject
{
	GENERATED_BODY()
	
public:
	UPROPERTY()
	UMaterialInterface* DefaultCameraOverlayMaterial;
	
	UPROPERTY()
	UMaterialInterface* DepthOcclusionOverlayMaterial;
	
	UPROPERTY()
	UMaterialInterface* MatteOcclusionOverlayMaterial;

	UARKitCameraOverlayMaterialLoader()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultOverlayMaterialRef(*OverlayMaterialPath);
		DefaultCameraOverlayMaterial = DefaultOverlayMaterialRef.Object;
		
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> DepthOcclusionOverlayMaterialRef(*DepthOcclusionOverlayMaterialPath);
		DepthOcclusionOverlayMaterial = DepthOcclusionOverlayMaterialRef.Object;
		
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatteOcclusionOverlayMaterialRef(*MatteOcclusionOverlayMaterialPath);
		MatteOcclusionOverlayMaterial = MatteOcclusionOverlayMaterialRef.Object;
	}
	
	static const FString OverlayMaterialPath;
	static const FString DepthOcclusionOverlayMaterialPath;
	static const FString MatteOcclusionOverlayMaterialPath;
};

class FAppleARKitVideoOverlay
	: public FGCObject
{
public:
	FAppleARKitVideoOverlay();
	virtual ~FAppleARKitVideoOverlay();

	void SetCameraTexture(UARTextureCameraImage* InCameraImage);

	void RenderVideoOverlay_RenderThread(FRHICommandListImmediate& RHICmdList, const FSceneView& InView, struct FAppleARKitFrame& Frame, const EDeviceScreenOrientation DeviceOrientation, const float WorldToMeterScale);
	bool GetPassthroughCameraUVs_RenderThread(TArray<FVector2D>& OutUVs, const EDeviceScreenOrientation DeviceOrientation);

	void SetOverlayTexture(UARTextureCameraImage* InCameraImage);
	void SetEnablePersonOcclusion(bool bEnable);

private:
	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//~ FGCObject
	
	void RenderVideoOverlayWithMaterial(FRHICommandListImmediate& RHICmdList, const FSceneView& InView, struct FAppleARKitFrame& Frame, const EDeviceScreenOrientation DeviceOrientation, UMaterialInstanceDynamic* RenderingOverlayMaterial, const bool bRenderingOcclusion);
	void UpdateOcclusionTextures(const FAppleARKitFrame& Frame);

	UMaterialInstanceDynamic* MID_CameraOverlay;

	// Cache UV offset to be used by GetPassthroughCameraUVs_RenderThread
	FVector2D UVOffset;

	FVertexBufferRHIRef OverlayVertexBufferRHI[2];
	FIndexBufferRHIRef IndexBufferRHI;
	
	bool bEnablePersonOcclusion = false;
	
#if SUPPORTS_ARKIT_3_0
	ARMatteGenerator* MatteGenerator = nullptr;
	id<MTLCommandQueue> CommandQueue = nullptr;
#endif
	
	bool bOcclusionDepthTextureRecentlyUpdated = false;
	UAppleARKitOcclusionTexture* OcclusionMatteTexture = nullptr;
	UAppleARKitOcclusionTexture* OcclusionDepthTexture = nullptr;
	UMaterialInstanceDynamic* MID_DepthOcclusionOverlay = nullptr;
	UMaterialInstanceDynamic* MID_MatteOcclusionOverlay = nullptr;
};

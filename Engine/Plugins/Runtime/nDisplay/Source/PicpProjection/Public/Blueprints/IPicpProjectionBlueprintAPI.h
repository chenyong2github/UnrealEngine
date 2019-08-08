// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RHI.h"
#include "RHIResources.h"

#include "Blueprints/PicpProjectionFrustumDataListener.h"
#include "Blueprints/ReprojectionBlendingData.h"
#include "OverlayRenderingParameters.h"

#include "PicpPostProcessing.h"

#include "IPicpProjectionBlueprintAPI.generated.h"

UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class UPicpProjectionBlueprintAPI : public UInterface
{
	GENERATED_BODY()
};

class IPicpProjectionBlueprintAPI
{
	GENERATED_BODY()

public:
	/**
	* Binds multiple device channels to multiple keys
	*
	* @return true if success
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Viewport Count"), Category = "PICP")
	virtual int GetViewportCount() = 0;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Projection Data Listener"), Category = "PICP")
	virtual void AddProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> Listener) = 0;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove Projection Data Listener"), Category = "PICP")
	virtual void RemoveProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> Listener) = 0;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Clean Projection Data Listener"), Category = "PICP")
	virtual void CleanProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> Listener) = 0;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Blend Frames"), Category = "PICP")
	virtual void BlendCameraFrameCaptures(UTexture* SrcFrame, UTextureRenderTarget2D* DstFrame, UTextureRenderTarget2D* Result) = 0;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Apply Blur Effect"), Category = "PICP")
	virtual void ApplyBlurPostProcess(UTextureRenderTarget2D* InOutRenderTarget, UTextureRenderTarget2D* TemporaryRenderTarget, int KernelRadius, float KernelScale, EPicpBlurPostProcessShaderType BlurType) = 0;
	
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Setup Overlay Captures"), Category = "PICP")
	virtual void SetupOverlayCaptures(const TArray<struct FPicpOverlayFrameBlendingParameters>& captures) = 0;		

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Capture Final Warped Frame"), Category = "PICP")
	virtual void SetWarpTextureCaptureState(UTextureRenderTarget2D* dstTexture, const FString& ViewportId, const int ViewIdx, bool bCaptureNow) = 0;

	/**
	 * Creates a new render target and initializes it to the specified dimensions
	 */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Enable mips for Texture Render Target"), Category = "PICP")
	virtual void EnableTextureRenderTargetMips(UTextureRenderTarget2D* Texture) = 0;
};

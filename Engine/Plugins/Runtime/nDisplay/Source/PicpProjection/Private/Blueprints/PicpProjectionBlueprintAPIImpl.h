// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include "Blueprints/IPicpProjectionBlueprintAPI.h"
#include "PicpProjectionBlueprintAPIImpl.generated.h"


/**
 * Blueprint API interface implementation
 */
UCLASS()
class UPicpProjectionAPIImpl
	: public UObject
	, public IPicpProjectionBlueprintAPI
{
	GENERATED_BODY()

public:
	/**
	* Binds multiple device channels to multiple keys
	*
	* @return true if success
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Viewport Count"), Category = "PICP")
	virtual int GetViewportCount() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Projection Data Listener"), Category = "PICP")
	virtual void AddProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> Listener) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove Projection Data Listener"), Category = "PICP")
	virtual void RemoveProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> Listener) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Clean Projection Data Listener"), Category = "PICP")
	virtual void CleanProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> Listener) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Blend Frames"), Category = "PICP")
	virtual void BlendCameraFrameCaptures(UTexture* SrcFrame, UTextureRenderTarget2D* DstFrame, UTextureRenderTarget2D* Result) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Apply Blur Effect"), Category = "PICP")
	virtual void ApplyBlurPostProcess(UTextureRenderTarget2D* InOutRenderTarget, UTextureRenderTarget2D* TemporaryRenderTarget, int KernelRadius, float KernelScale, EPicpBlurPostProcessShaderType BlurType) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Setup Overlay Captures"), Category = "PICP")
	virtual void SetupOverlayCaptures(const TArray<struct FPicpOverlayFrameBlendingParameters>& captures) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Capture Final Warped Frame"), Category = "PICP")
	virtual void SetWarpTextureCaptureState(UTextureRenderTarget2D* dstTexture, const FString& ViewportId, const int ViewIdx, bool bCaptureNow) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Enable mips for Texture Render Target"), Category = "PICP")
	virtual void EnableTextureRenderTargetMips(UTextureRenderTarget2D* Texture) override;
};
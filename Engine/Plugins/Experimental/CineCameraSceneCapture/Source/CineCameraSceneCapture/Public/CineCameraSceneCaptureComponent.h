// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneCaptureComponent2D.h"
#include "SceneViewExtension.h"
#include "CineCameraSceneCaptureComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCineCapture, Log, All);

class UCineCameraComponent;

class FCineCameraCaptureSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FCineCameraCaptureSceneViewExtension(const FAutoRegister& AutoRegister);

	//~ Begin FSceneViewExtensionBase Interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};
	//~ End FSceneViewExtensionBase Interface
public:
	/** 
	* Flags all views that will be setup until (bSetupViewInProcess == false) as related to Cine Capture.
	*/
	void OnViewSetupStarted(TSoftObjectPtr<UCineCameraComponent> InCineCameraComponent, bool bInFollowSceneCaptureRenderPath)
	{ 
		bSetupViewInProcess = true; 
		CineCameraComponent = InCineCameraComponent;
		bFollowSceneCaptureRenderPath = bInFollowSceneCaptureRenderPath;
	};

	/**
	* Flags all following view setups as not related to Cine Capture.
	*/
	void OnViewSetupFinished() { bSetupViewInProcess = false; };

	/**
	* Delta time is needed for the purposes of camera smoothing.
	*/
	void SetFrameDeltaTime(float InDeltaTime)
	{
		DeltaTime = InDeltaTime;
	}
private:
	/** 
	* Translates to View.bIsSceneCapture.
	*/
	bool bFollowSceneCaptureRenderPath;

	/** 
	* When this property is true, it means that the views that are being setup currently are related to Cine Capture.
	*/
	bool bSetupViewInProcess;

	/**
	* A transient property that is used to deliver settings from CineCameraComponent to views that are related to cine capture.
	*/
	TSoftObjectPtr<UCineCameraComponent> CineCameraComponent;

	/** 
	* Delta time between frames. Used for camera smoothing.
	*/
	float DeltaTime;
};

/**
* Cine Capture Component extends Scene Capture to allow users to render Cine Camera Component into a render target. 
* Cine Capture has a few modifiable properties, but most of the properties are controlled by Cine Camera Component.
* Cine Capture Component is required to be parented to Cine Camera Component or a class that extends it.
* 
*/
UCLASS(hidecategories = (Transform, Collision, Object, Physics, SceneComponent, PostProcessVolume, Projection, Rendering, PlanarReflection), ClassGroup = Rendering, editinlinenew, meta = (BlueprintSpawnableComponent))
class UCineCaptureComponent2D : public USceneCaptureComponent2D
{
	GENERATED_UCLASS_BODY()
public:
	
	/** 
	* Highest possible dimension of specified render target in either X or Y (depends on cine camera aspect ratio). Used to adjust the render target size.
	* Example: With cine camera's censor aspect ratio is 2:1, and Highest Dimension set to 1000, the render target will be resized to 1000x500. 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture Settings", meta = (ClampMin = "1", ClampMax = "8192", UIMin = "1", UIMax = "8192"))
	int32 RenderTargetHighestDimension;

	/** 
	* Affects rendering path cine capture takes. Scene Capture takes a slightly different rendering route compared to viewport rendering
	* for the purposes of optimization. If the results of cine capture are vastly different to what cine camera displays try disabling this.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture Settings", AdvancedDisplay)
	bool bFollowSceneCaptureRenderPath;

	UPROPERTY()
	TSoftObjectPtr<UCineCameraComponent> CineCameraComponent;

public:
	void CheckResizeRenderTarget();
public:
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	/** Used for validation of this componet's attachment to Cine Camera Component. */
	virtual void OnAttachmentChanged() override; 
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	void ValidateCineCameraComponent();

private:
	/** This scene view extension is used to get ahold of views during the setup process. */
	TSharedPtr<FCineCameraCaptureSceneViewExtension, ESPMode::ThreadSafe> CineCaptureSVE;
};

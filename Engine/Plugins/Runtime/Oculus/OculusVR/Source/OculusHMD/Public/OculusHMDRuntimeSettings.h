// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OculusHMDTypes.h"
#include "OculusFunctionLibrary.h"
#include "OculusHMDRuntimeSettings.generated.h"

/**
* Implements the settings for the OculusVR plugin.
*/
UCLASS(config = Engine, defaultconfig)
class OCULUSHMD_API UOculusHMDRuntimeSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	
	/** Whether the Splash screen is enabled. */
	UPROPERTY(config, EditAnywhere, Category = SplashScreen)
	bool bAutoEnabled;

	/** An array of splash screen descriptors listing textures to show and their positions. */
	UPROPERTY(config, EditAnywhere, Category = SplashScreen)
	TArray<FOculusSplashDesc> SplashDescs;

	/** If enabled, the target HMD will perform a color space transformation */
	UPROPERTY(config, EditAnywhere, Category = General)
	bool bEnableSpecificColorGamut;

	/** The target color space */
	UPROPERTY(config, EditAnywhere, Category = General)
	EColorSpace	ColorSpace;

	/** Whether Dash is supported by the app, which will keep the app in foreground when the User presses the oculus button (needs the app to handle input focus loss!) */
	UPROPERTY(config, EditAnywhere, Category = PC)
	bool bSupportsDash;

	/** Whether the app's depth buffer is shared with the Rift Compositor, for layer (including Dash) compositing, PTW, and potentially more. */
	UPROPERTY(config, EditAnywhere, Category = PC)
	bool bCompositesDepth;

	/** Computes mipmaps for the eye buffers every frame, for a higher quality distortion */
	UPROPERTY(config, EditAnywhere, Category = PC)
	bool bHQDistortion;

	/** Minimum allowed pixel density. */
	UPROPERTY(config, EditAnywhere, Category = PC)
	float PixelDensityMin;

	/** Maximum allowed pixel density. */
	UPROPERTY(config, EditAnywhere, Category = PC)
	float PixelDensityMax;

	/** Default CPU level controlling CPU frequency on the mobile device */
	UPROPERTY(config, EditAnywhere, Category = Mobile)
	int CPULevel;

	/** Default GPU level controlling GPU frequency on the mobile device */
	UPROPERTY(config, EditAnywhere, Category = Mobile)
	int GPULevel;

	/** Default Fixed Foveated Rendering level for Oculus Quest */
	UPROPERTY(config, EditAnywhere, Category = Mobile)
	EFixedFoveatedRenderingLevel FFRLevel;

	/** Whether FFR adjusts its level dynamically or not */
	UPROPERTY(config, EditAnywhere, Category = Mobile)
	bool FFRDynamic;

	/** Compensates in the compositor for chromatic aberration, at a higher GPU cost but without the color fringes on the sides of the lenses */
	UPROPERTY(config, EditAnywhere, Category = Mobile)
	bool bChromaCorrection;

	/** Recenters the HMD too when the controller recenter button is pressed on Go */
	UPROPERTY(config, EditAnywhere, Category = Mobile)
	bool bRecenterHMDWithController;

	/** If enabled the app will be focus aware. This will keep the app in foreground when the User presses the oculus button (needs the app to handle input focus loss!) */
	UPROPERTY(config, EditAnywhere, Category = Mobile)
	bool bFocusAware;

	/** [Experimental]Enable Late latching for reducing HMD and controller latency, improve tracking prediction quality, multiview and vulkan must be enabled for this feature. */
	UPROPERTY(config, EditAnywhere, Category = Mobile)
	bool bLateLatching;

	/** If enabled the app will use the Oculus system keyboard for input fields. This requires that the app be focus aware. */
	UPROPERTY(config, EditAnywhere, Category = Mobile)
	bool bRequiresSystemKeyboard;

	/** Whether controllers and/or hands can be used with the app */
	UPROPERTY(config, EditAnywhere, Category = Mobile)
	EHandTrackingSupport HandTrackingSupport;

	/** Enable phase sync on mobile, reducing HMD and controller latency, improve tracking prediction quality */
	UPROPERTY(config, EditAnywhere, Category = Mobile)
	bool bPhaseSync;

private:
	void LoadFromIni();

};

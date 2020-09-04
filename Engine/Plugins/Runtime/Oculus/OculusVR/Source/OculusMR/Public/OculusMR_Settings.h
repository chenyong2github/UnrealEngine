// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/ObjectMacros.h"

#include "OculusMR_Settings.generated.h"

UENUM(BlueprintType)
enum class EOculusMR_CameraDeviceEnum : uint8
{
	CD_None         UMETA(DisplayName = "None"),
	CD_WebCamera0   UMETA(DisplayName = "Web Camera 0"),
	CD_WebCamera1   UMETA(DisplayName = "Web Camera 1"),
};

UENUM(BlueprintType)
enum class EOculusMR_ClippingReference : uint8
{
	CR_TrackingReference    UMETA(DisplayName = "Tracking Reference"),
	CR_Head                 UMETA(DisplayName = "Head"),
};

UENUM(BlueprintType)
enum class EOculusMR_PostProcessEffects : uint8
{
	PPE_Off             UMETA(DisplayName = "Off"),
	PPE_On				UMETA(DisplayName = "On"),
};

UENUM(BlueprintType)
enum class EOculusMR_CompositionMethod : uint8
{
	/* Generate both foreground and background views for compositing with 3rd-party software like OBS. */
	ExternalComposition		UMETA(DisplayName = "External Composition"),
	/* Composite the camera stream directly to the output with the proper depth.*/
	DirectComposition		UMETA(DisplayName = "Direct Composition")
};

UCLASS(ClassGroup = OculusMR, Blueprintable)
class UOculusMR_Settings : public UObject
{
	GENERATED_BODY()

public:
	UOculusMR_Settings(const FObjectInitializer& ObjectInitializer);

	/** Specify the distance to the camera which divide the background and foreground in MxR casting.
	  * Set it to CR_TrackingReference to use the distance to the Tracking Reference, which works better
	  * in the stationary experience. Set it to CR_Head would use the distance to the HMD, which works better
	  * in the room scale experience.
	  */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite)
	EOculusMR_ClippingReference ClippingReference;

	/** The casting viewports would use the same resolution of the camera which used in the calibration process. */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite)
	bool bUseTrackedCameraResolution;

	/** When bUseTrackedCameraResolution is false, the width of each casting viewport */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite)
	int WidthPerView;

	/** When bUseTrackedCameraResolution is false, the height of each casting viewport */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite)
	int HeightPerView;

	/** When CompositionMethod is External Composition, the latency of the casting output which could be adjusted to
	  * match the camera latency in the external composition application */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite, meta = (UIMin = "0.0", UIMax = "0.1"))
	float CastingLatency;

	/** When CompositionMethod is External Composition, the color of the backdrop in the foreground view */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite)
	FColor BackdropColor;

	/** When CompositionMethod is Direct Composition, you could adjust this latency to delay the virtual
	* hand movement by a small amount of time to match the camera latency */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite, meta = (UIMin = "0.0", UIMax = "0.5"))
	float HandPoseStateLatency;

	/** [Green-screen removal] Chroma Key Color. Apply when CompositionMethod is DirectComposition */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite)
	FColor ChromaKeyColor;

	/** [Green-screen removal] Chroma Key Similarity. Apply when CompositionMethod is DirectComposition */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite, meta = (UIMin = "0.0", UIMax = "1.0"))
	float ChromaKeySimilarity;

	/** [Green-screen removal] Chroma Key Smooth Range. Apply when CompositionMethod is DirectComposition */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite, meta = (UIMin = "0.0", UIMax = "0.2"))
	float ChromaKeySmoothRange;

	/** [Green-screen removal] Chroma Key Spill Range. Apply when CompositionMethod is DirectComposition */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite, meta = (UIMin = "0.0", UIMax = "0.2"))
	float ChromaKeySpillRange;

	/** Set the amount of post process effects in the MR view for external composition */
	UPROPERTY(Category = OculusMR, EditAnywhere, BlueprintReadWrite)
	EOculusMR_PostProcessEffects ExternalCompositionPostProcessEffects;


	/** ExternalComposition: The casting window includes the background and foreground view
	  * DirectComposition: The game scene would be composited with the camera frame directly
	  */
	UFUNCTION(BlueprintCallable, Category = OculusMR)
	EOculusMR_CompositionMethod GetCompositionMethod() { return CompositionMethod; }

	/** ExternalComposition: The casting window includes the background and foreground view
	  * DirectComposition: The game scene would be composited with the camera frame directly
	  */
	UFUNCTION(BlueprintCallable, Category = OculusMR)
	void SetCompositionMethod(EOculusMR_CompositionMethod val);

	/** When CompositionMethod is DirectComposition, the physical camera device which provide the frame */
	UFUNCTION(BlueprintCallable, Category = OculusMR)
	EOculusMR_CameraDeviceEnum GetCapturingCamera() { return CapturingCamera; }

	/** When CompositionMethod is DirectComposition, the physical camera device which provide the frame */
	UFUNCTION(BlueprintCallable, Category = OculusMR)
	void SetCapturingCamera(EOculusMR_CameraDeviceEnum val);

	/** Is MRC on and off */
	UFUNCTION(BlueprintCallable, Category = OculusMR)
	bool GetIsCasting() { return bIsCasting; }

	/** Turns MRC on and off */
	UFUNCTION(BlueprintCallable, Category = OculusMR)
	void SetIsCasting(bool val);

	/** Bind the casting camera to the calibrated external camera.
	  * (Requires a calibrated external camera)
	  */
	UFUNCTION(BlueprintCallable, Category = OculusMR)
	void BindToTrackedCameraIndexIfAvailable(int InTrackedCameraIndex);

	UFUNCTION(BlueprintCallable, Category = OculusMR)
	int GetBindToTrackedCameraIndex() { return BindToTrackedCameraIndex; }

	/** Load settings from the config file */
	UFUNCTION(BlueprintCallable, Category = OculusMR)
	void LoadFromIni();

	/** Save settings to the config file */
	UFUNCTION(BlueprintCallable, Category = OculusMR)
	void SaveToIni() const;

private:
	/** Turns MRC on and off (does not get saved to or loaded from ini) */
	UPROPERTY()
	bool bIsCasting;

	/** ExternalComposition: The casting window includes the background and foreground view
	  * DirectComposition: The game scene would be composited with the camera frame directly
	  */
	UPROPERTY()
	EOculusMR_CompositionMethod CompositionMethod;

	/** When CompositionMethod is DirectComposition, the physical camera device which provide the frame */
	UPROPERTY()
	EOculusMR_CameraDeviceEnum CapturingCamera;

	/** Tracked camera that we want to bind the in-game MR camera to*/
	int BindToTrackedCameraIndex;

	DECLARE_DELEGATE_TwoParams(OnCompositionMethodChangeDelegate, EOculusMR_CompositionMethod, EOculusMR_CompositionMethod);
	DECLARE_DELEGATE_TwoParams(OnCapturingCameraChangeDelegate, EOculusMR_CameraDeviceEnum, EOculusMR_CameraDeviceEnum);
	DECLARE_DELEGATE_TwoParams(OnBooleanSettingChangeDelegate, bool, bool);
	DECLARE_DELEGATE_TwoParams(OnIntegerSettingChangeDelegate, int, int);

	OnIntegerSettingChangeDelegate TrackedCameraIndexChangeDelegate;
	OnCompositionMethodChangeDelegate CompositionMethodChangeDelegate;
	OnCapturingCameraChangeDelegate CapturingCameraChangeDelegate;
	OnBooleanSettingChangeDelegate IsCastingChangeDelegate;

	// Give the OculusMR module access to the delegates so that 
	friend class FOculusMRModule;
};
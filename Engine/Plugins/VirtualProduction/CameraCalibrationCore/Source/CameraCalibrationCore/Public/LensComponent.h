// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"

#include "CameraCalibrationTypes.h"
#include "LensFile.h"
#include "LiveLinkComponentController.h"

#include "LensComponent.generated.h"

class UCineCameraComponent;

/** Mode that controls where FIZ inputs are sourced from and how they are used to evaluate the LensFile */
UENUM(BlueprintType)
enum class EFIZEvaluationMode : uint8
{
	/** Evaluate the Lens File with the latest FIZ data received from LiveLink */
	UseLiveLink,
	/** Evaluate the Lens File using the current FIZ settings of the target camera */
	UseCameraSettings,
	/** Evaluate the Lens File using values recorded in a level sequence (set automatically when the sequence is opened) */
	UseRecordedValues,
	/** Do not evaluate the Lens File */
	DoNotEvaluate,
};

/** Component for applying a post-process lens distortion effect to a CineCameraComponent on the same actor */
UCLASS(HideCategories=(Tags, Activation, Cooking, AssetUserData, Collision), meta=(BlueprintSpawnableComponent))
class CAMERACALIBRATIONCORE_API ULensComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULensComponent();

	//~ Begin UActorComponent interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void DestroyComponent(bool bPromoteChildren = false) override;
	//~ End UActorComponent interface

	//~ Begin UObject interface
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostEditImport() override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR
	//~ End UObject interface

	/** Get the LensFile used by this component */
	UFUNCTION(BlueprintPure, Category="Lens Component")
	ULensFile* GetLensFile() const;

	/** Set the LensFile used by this component */
	UFUNCTION(BlueprintCallable, Category="Lens Component")
	void SetLensFile(FLensFilePicker LensFile);

	/** Get the evaluation mode used to evaluate the LensFile */
	UFUNCTION(BlueprintPure, Category="Lens Component")
	EFIZEvaluationMode GetFIZEvaluationMode() const;

	/** Set the evaluation mode used to evaluate the LensFile */
	UFUNCTION(BlueprintCallable, Category="Lens Component", meta=(DisplayName="Set FIZ Evaluation Mode"))
	void SetFIZEvaluationMode(EFIZEvaluationMode Mode);

	/** Returns true if nodal offset will be automatically applied during this component's tick, false otherwise */
	UFUNCTION(BlueprintPure, Category="Lens Component")
	bool ShouldApplyNodalOffsetOnTick() const;

	/** Set whether nodal offset should be automatically applied during this component's tick */
	UFUNCTION(BlueprintCallable, Category="Lens Component")
	void SetApplyNodalOffsetOnTick(bool bApplyNodalOffset);

	/** Returns true if nodal offset was applied during the current tick, false otherwise */
	UFUNCTION(BlueprintPure, Category = "Lens Component")
	bool WasNodalOffsetAppliedThisTick() const;

	/** 
	 * Manually apply nodal offset to the specified component. 
	 * If bUseManualInputs is true, the input Focus and Zoom values will be used to evaluate the LensFile .
	 * If bUseManualInputs is false, the LensFile be will evaluated based on the Lens Component's evaluation mode.
	 */
	UFUNCTION(BlueprintCallable, Category="Lens Component", meta=(AdvancedDisplay=1))
	void ApplyNodalOffset(USceneComponent* ComponentToOffset, bool bUseManualInputs = false, float ManualFocusInput = 0.0f, float ManualZoomInput = 0.0f);

protected:
	UE_DEPRECATED(5.1, "The use of this callback by this class has been deprecated and it is no longer registered. You can register your own delegate with FWorldDelegates::OnWorldPostActorTick")
	void OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

private:
	/** Evaluate the LensFile for nodal offset (using the current evaluation mode) and apply it to the latest component to offset */
	void ApplyNodalOffset();

	/** If TargetCameraComponent is not set, initialize it to the first CineCameraComponent on the same actor as this component */
	void InitDefaultCamera();

	/** Remove the last distortion MID applied to the input CineCameraComponent and reset its FOV to use no overscan */
	void CleanupDistortion(UCineCameraComponent* CineCameraComponent);

	/** Register a new lens distortion handler with the camera calibration subsystem using the selected lens file */
	void CreateDistortionHandlerForLensFile();

	/** Register to the new LiveLink component's callback to be notified when its controller map changes */
	void OnLiveLinkComponentRegistered(ULiveLinkComponentController* LiveLinkComponent);
	
	/** Callback executed when a LiveLink component on the same actor ticks */
	void ProcessLiveLinkData(const ULiveLinkComponentController* const LiveLinkComponent, const FLiveLinkSubjectFrameData& SubjectData);

	/** Inspects the subject data and LiveLink transform controller to determine which component (if any) had tracking data applied to it */
	void UpdateTrackedComponent(const ULiveLinkComponentController* const LiveLinkComponent, const FLiveLinkSubjectFrameData& SubjectData);

	/** Inspects the subject data and LiveLink camera controller cache the FIZ that was input for the target camera */
	void UpdateLiveLinkFIZ(const ULiveLinkComponentController* const LiveLinkComponent, const FLiveLinkSubjectFrameData& SubjectData);

	/** Updates the focus and zoom inputs that will be used to evaluate the LensFile based on the evaluation mode */
	void UpdateLensFileEvaluationInputs();

protected:
	/** Lens File used to drive distortion with current camera settings */
	UPROPERTY(EditAnywhere, Category="Lens File", meta=(ShowOnlyInnerProperties))
	FLensFilePicker LensFilePicker;

	/** Specify how the Lens File should be evaluated */
	UPROPERTY(EditAnywhere, Category="Lens File")
	EFIZEvaluationMode EvaluationMode = EFIZEvaluationMode::UseLiveLink;

	/** The CineCameraComponent on which to apply the post-process distortion effect */
	UPROPERTY(EditInstanceOnly, AdvancedDisplay, Category="Lens File", meta=(UseComponentPicker, AllowedClasses="/Script/CinematicCamera.CineCameraComponent"))
	FComponentReference TargetCameraComponent;

	/** 
	 * If checked, nodal offset will be applied automatically when this component ticks. 
	 * Set to false if nodal offset needs to be manually applied at some other time (via Blueprints).
	 */
	UPROPERTY(EditAnywhere, Category="Nodal Offset")
	bool bApplyNodalOffsetOnTick = true;

	/** Structure used to query the camera calibration subsystem for a lens distortion model handler */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distortion")
	FDistortionHandlerPicker DistortionSource;

	/** Whether or not to apply distortion to the target camera component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distortion")
	bool bApplyDistortion = false;

	/**
	 * Whether to use the specified lens file to drive distortion
	 * Enabling this will create a new distortion source and automatically set this component to use it
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category="Distortion")
	bool bEvaluateLensFileForDistortion = false;

	/** Whether to scale the computed overscan by the overscan percentage */
	UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Category="Distortion")
	bool bScaleOverscan = false;

	/** The percentage of the computed overscan that should be applied to the target camera */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category="Distortion", meta=(EditCondition="bScaleOverscan", ClampMin="0.0", ClampMax="2.0"))
	float OverscanMultiplier = 1.0f;

	/** Whether a distortion effect is currently being applied to the target camera component */
	UPROPERTY()
	bool bIsDistortionSetup = false;

	/** Focal length of the target camera before any overscan has been applied */
	UPROPERTY()
	float OriginalFocalLength = 35.0f;

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.1, "This property has been deprecated. The LensDistortion component no longer tracks the attached camera's original rotation.")
	UPROPERTY()
	FRotator OriginalCameraRotation_DEPRECATED;

	UE_DEPRECATED(5.1, "This property has been deprecated. The LensDistortion component no longer tracks the attached camera's original location.")
	UPROPERTY()
	FVector OriginalCameraLocation_DEPRECATED;
#endif //WITH_EDITORONLY_DATA

	/** Cached MID last applied to the target camera */
	UPROPERTY()
	UMaterialInstanceDynamic* LastDistortionMID = nullptr;

	/** Cached most recent target camera, used to clean up the old camera when the user changes the target */
	UPROPERTY(Transient)
	UCineCameraComponent* LastCameraComponent = nullptr;

	/** Distortion handler produced by this component */
	UPROPERTY(Transient)
	ULensDistortionModelHandlerBase* ProducedLensDistortionHandler = nullptr;

	/** Unique identifier representing the source of distortion data */
	UPROPERTY(DuplicateTransient)
	FGuid DistortionProducerID;

	/** Inputs to LensFile evaluation */
	UPROPERTY(Interp, VisibleAnywhere, AdvancedDisplay, Category="Lens File")
	float EvalFocus = 0.0f;
	
	UPROPERTY(Interp, VisibleAnywhere, AdvancedDisplay, Category="Lens File")
	float EvalZoom = 0.0f;

private:
	/** Scene component that should have nodal offset applied */
	TWeakObjectPtr<USceneComponent> TrackedComponent;

	/** Latest LiveLink FIZ data, used to evaluate the LensFile */
	FLensFileEvalData LiveLinkFIZ;

	/** Whether or not nodal offset was applied to a tracked component this tick */
	bool bWasNodalOffsetAppliedThisTick = false;
};

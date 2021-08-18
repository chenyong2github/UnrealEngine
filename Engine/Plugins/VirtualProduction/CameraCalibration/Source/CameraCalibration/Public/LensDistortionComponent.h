// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"

#include "CameraCalibrationTypes.h"
#include "LensFile.h"

#include "LensDistortionComponent.generated.h"

class UCineCameraComponent;

/** Component for applying a post-process lens distortion effect to a CineCameraComponent on the same actor */
UCLASS(HideCategories = (Tags, Activation, Cooking, AssetUserData, Collision), meta = (BlueprintSpawnableComponent))
class CAMERACALIBRATION_API ULensDistortionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULensDistortionComponent();

	//~ Begin UActorComponent interface
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void DestroyComponent(bool bPromoteChildren = false) override;
	//~ End UActorComponent interface

	//~ UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostEditImport() override;
	virtual void PostLoad() override;
	//~ End UObject interface

protected:
	/** Verify base transform and apply nodal offset on top of everything else done in tick */
	void OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

private:
	/** If TargetCameraComponent is not set, initialize it to the first CineCameraComponent on the same actor as this component */
	void InitDefaultCamera();

	/** Remove the last distortion MID applied to the input CineCameraComponent and reset its FOV to use no overscan */
	void CleanupDistortion(UCineCameraComponent* CineCameraComponent);

	/** Register a new lens distortion handler with the camera calibration subsystem using the selected lens file */
	void CreateDistortionHandlerForLensFile();

	/** Evaluate the input LensFile and apply the nodal offset to the input camera's transform */
	void ApplyNodalOffset(ULensFile* SelectedLensFile, UCineCameraComponent* CineCameraComponent);

protected:
	/** The CineCameraComponent on which to apply the post-process distortion effect */
	UPROPERTY(EditInstanceOnly, Category = "Default", meta = (UseComponentPicker, AllowedClasses = "CineCameraComponent"))
	FComponentReference TargetCameraComponent;

	/** Structure used to query the camera calibration subsystem for a lens distortion model handler */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
	FDistortionHandlerPicker DistortionSource;

	/** Whether or not to apply distortion to the target camera component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
	bool bApplyDistortion = false;

	/** Whether a distortion effect is currently being applied to the target camera component */
	UPROPERTY()
	bool bIsDistortionSetup = false;

	/** Focal length of the target camera before any overscan has been applied */
	UPROPERTY()
	float OriginalFocalLength = 35.0f;

	/** Original camera rotation to reset before/after applying nodal offset */
	UPROPERTY()
	FRotator OriginalCameraRotation;

	/** Original camera location to reset before/after applying nodal offset */
	UPROPERTY()
	FVector OriginalCameraLocation;

	/** Cached MID last applied to the target camera */
	UPROPERTY()
	UMaterialInstanceDynamic* LastDistortionMID = nullptr;

	/** Cached most recent target camera, used to clean up the old camera when the user changes the target */
	UPROPERTY(Transient)
	UCineCameraComponent* LastCameraComponent = nullptr;

	/** 
	 * Whether to use the specified lens file to drive distortion
	 * Enabling this will create a new distortion source and automatically set this component to use it
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Default")
	bool bEvaluateLensFileForDistortion = false;

	/** Whether to apply nodal offset to the target camera */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Default", meta = (EditCondition = "bEvaluateLensFileForDistortion"))
	bool bApplyNodalOffset = true;

	/** Lens File used to drive distortion with current camera settings */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Default", meta = (EditCondition="bEvaluateLensFileForDistortion"))
	FLensFilePicker LensFilePicker;

	/** Whether to scale the computed overscan by the overscan percentage */
	UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Category = "Default")
	bool bScaleOverscan = false;

	/** The percentage of the computed overscan that should be applied to the target camera */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "Default", meta = (EditCondition = "bScaleOverscan", ClampMin = "0.0", ClampMax = "2.0"))
	float OverscanMultiplier = 1.0f;

	/** Distortion handler produced by this component */
	UPROPERTY(Transient)
	ULensDistortionModelHandlerBase* ProducedLensDistortionHandler = nullptr;

	/** Unique identifier representing the source of distortion data */
	UPROPERTY(DuplicateTransient)
	FGuid DistortionProducerID;

private:
	/** Cached rotation, used to track changes to the camera transform, which impacts the original rotation used by nodal offset */
	FRotator LastRotation;

	/** Cached location, used to track changes to the camera transform, which impacts the original location used by nodal offset */
	FVector LastLocation;
};
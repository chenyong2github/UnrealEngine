// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkControllerBase.h"

#include "CineCameraComponent.h"
#include "Controllers/LiveLinkTransformController.h"
#include "Engine/EngineTypes.h"
#include "LensDistortionModelHandlerBase.h"
#include "LensFile.h"

#include "LiveLinkCameraController.generated.h"


struct FLiveLinkCameraStaticData;
struct FLiveLinkCameraFrameData;

/** Flags to control whether incoming values from LiveLink Camera FrameData should be applied or not */
USTRUCT()
struct FLiveLinkCameraControllerUpdateFlags
{
	GENERATED_BODY()

	/** Whether to apply FOV if it's available in LiveLink FrameData */
	UPROPERTY(EditAnywhere, Category = "Updates")
	bool bApplyFieldOfView = true;
	
	/** Whether to apply Aspect Ratio if it's available in LiveLink FrameData */
	UPROPERTY(EditAnywhere, Category = "Updates")
	bool bApplyAspectRatio = true;

	/** Whether to apply Focal Length if it's available in LiveLink FrameData */
	UPROPERTY(EditAnywhere, Category = "Updates")
	bool bApplyFocalLength = true;

	/** Whether to apply Projection Mode if it's available in LiveLink FrameData */
	UPROPERTY(EditAnywhere, Category = "Updates")
	bool bApplyProjectionMode= true;

	/** Whether to apply Filmback if it's available in LiveLink StaticData */
	UPROPERTY(EditAnywhere, Category = "Updates")
	bool bApplyFilmBack = true;

	/** Whether to apply Aperture if it's available in LiveLink FrameData */
	UPROPERTY(EditAnywhere, Category = "Updates")
	bool bApplyAperture = true;

	/** Whether to apply Focus Distance if it's available in LiveLink FrameData */
	UPROPERTY(EditAnywhere, Category = "Updates")
	bool bApplyFocusDistance = true;
};

/**
 */
UCLASS()
class LIVELINKCAMERA_API ULiveLinkCameraController : public ULiveLinkControllerBase
{
	GENERATED_BODY()

public:
	ULiveLinkCameraController();

	//~ Begin ULiveLinkControllerBase interface
	virtual void Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData) override;
	virtual bool IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport) override;
	virtual TSubclassOf<UActorComponent> GetDesiredComponentClass() const override;
	virtual void SetAttachedComponent(UActorComponent* ActorComponent) override;
	virtual void Cleanup() override;
	//~ End ULiveLinkControllerBase interface

	//~ Begin UObject interface
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostEditImport() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif	
	//~ End UObject interface

	/** Returns a const reference to input data used to evaluate the lens file */
	const FLensFileEvalData& GetLensFileEvalDataRef() const;

	UE_DEPRECATED(5.1, "This function has been deprecated. Nodal Offset is now applied by the LensComponent")
	void SetApplyNodalOffset(bool bInApplyNodalOffset);

protected:
	/** Applies FIZ data coming from LiveLink stream. Lens file is used if encoder mapping is required  */
	void ApplyFIZ(ULensFile* LensFile, UCineCameraComponent* CineCameraComponent, const FLiveLinkCameraStaticData* StaticData, const FLiveLinkCameraFrameData* FrameData);

	UE_DEPRECATED(5.1, "This function has been deprecated. Nodal Offset is now applied by the LensComponent")
	void ApplyNodalOffset(ULensFile* LensFile, UCineCameraComponent* CineCameraComponent);

	/** Update distortion state */
	void ApplyDistortion(ULensFile* LensFile, UCineCameraComponent* CineCameraComponent, const FLiveLinkCameraStaticData* StaticData, const FLiveLinkCameraFrameData* FrameData);

	UE_DEPRECATED(5.1, "The use of this callback by this class has been deprecated and it is no longer registered. You can register your own delegate with FWorldDelegates::OnWorldPostActorTick")
	void OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	/** 
	 * If part of FIZ is not streamed, verify that LensFile associated tables have only one entry 
	 * Used to warn user of potential problem evaluating LensFile
	 */
	void VerifyFIZWithLensFileTables(ULensFile* LensFile, const FLiveLinkCameraStaticData* StaticData) const;

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FComponentReference ComponentToControl_DEPRECATED;

	UPROPERTY()
	FLiveLinkTransformControllerData TransformData_DEPRECATED;
#endif

	/**
	 * Should LiveLink inputs be remapped (i.e normalized to physical units) using camera component range
	 */
	UPROPERTY(EditAnywhere, Category = "Camera Calibration")
	bool bUseCameraRange = false;

	/** Asset containing encoder and fiz mapping */
	UPROPERTY(EditAnywhere, Category = "Camera Calibration")
	FLensFilePicker LensFilePicker;

	/** Whether to use the cropped filmback setting to drive the filmback of the attached camera component */
	UPROPERTY(EditAnywhere, Category = "Camera Calibration")
	bool bUseCroppedFilmback = false;

	/** 
	 * If a LensFile is being evaluated, the filmback saved in that LensFile will drive the attached camera component to ensure correct calibration.
	 * If bUseCroppedFilmback is true, this value will be applied to the camera component and used to evaluate the LensFile instead.
	 */
	UPROPERTY(EditAnywhere, Category = "Camera Calibration")
	FCameraFilmbackSettings CroppedFilmback;

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.1, "This property has been deprecated. The LiveLink camera controller no longer applies nodal offset.")
	UPROPERTY()
	bool bApplyNodalOffset_DEPRECATED = true;
#endif //WITH_EDITORONLY_DATA

	/** Whether to scale the computed overscan by the overscan percentage */
	UPROPERTY(BlueprintReadWrite, Category = "Camera Calibration")
	bool bScaleOverscan = false;

	/** The percentage of the computed overscan that should be applied to the target camera */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Calibration", meta = (EditCondition = "bScaleOverscan", ClampMin = "0.0", ClampMax = "2.0"))
	float OverscanMultiplier = 1.0f;

protected:
	/** Cached distortion handler associated with attached camera component */
	UPROPERTY(Transient)
	ULensDistortionModelHandlerBase* LensDistortionHandler = nullptr;

	/** Unique identifier representing the source of distortion data */
	UPROPERTY(DuplicateTransient)
	FGuid DistortionProducerID;

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.1, "This property has been deprecated. The LiveLink camera controller no longer applies nodal offset.")
	UPROPERTY()
	FRotator OriginalCameraRotation_DEPRECATED;

	UE_DEPRECATED(5.1, "This property has been deprecated. The LiveLink camera controller no longer applies nodal offset.")
	UPROPERTY()
	FVector OriginalCameraLocation_DEPRECATED;
#endif //WITH_EDITORONLY_DATA

	/** Used to control which data from LiveLink is actually applied to camera */
	UPROPERTY(EditAnywhere, Category = "Settings")
	FLiveLinkCameraControllerUpdateFlags UpdateFlags;

#if WITH_EDITORONLY_DATA
	/** Whether to refresh frustum drawing on value change */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bShouldUpdateVisualComponentOnChange = true;
#endif

protected:
	/** Caches the latest inputs to the LensFile evaluation */
	FLensFileEvalData LensFileEvalData;

private:
	//Last values used to detect changes made by the user and update our original caches
	FCameraFilmbackSettings LastFilmback;

	double LastLensTableVerificationTimestamp = 0.0;
};

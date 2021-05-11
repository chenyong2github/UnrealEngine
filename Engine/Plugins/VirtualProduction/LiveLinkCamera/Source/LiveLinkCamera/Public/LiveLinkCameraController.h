// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkControllerBase.h"

#include "CineCameraComponent.h"
#include "Controllers/LiveLinkTransformController.h"
#include "Engine/EngineTypes.h"
#include "LensDistortionModelHandlerBase.h"
#include "LensFile.h"
#include "Materials/MaterialInstanceDynamic.h"

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
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FComponentReference ComponentToControl_DEPRECATED;

	UPROPERTY()
	FLiveLinkTransformControllerData TransformData_DEPRECATED;
#endif

	/** Should encoder mapping (normalized to physical units) be done using lens file or camera component range */
	UPROPERTY(EditAnywhere, Category = "Camera Calibration")
	bool bUseLensFileForEncoderMapping = true;

	/** Asset containing encoder and fiz mapping */
	UPROPERTY(EditAnywhere, Category = "Camera Calibration")
	FLensFilePicker LensFilePicker;

	/** Apply nodel offset from lens file if enabled */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera Calibration")
	bool bApplyNodalOffset = true;

protected:
	/** Whether or not to apply a post-process distortion effect directly to the attached CineCamera */
	UPROPERTY(EditAnywhere, Category = "Camera Calibration")
	bool bApplyDistortion = false;

	/** Cached distortion handler associated with attached camera component */
	UPROPERTY(EditAnywhere, Category = "Camera Calibration", Transient)
	ULensDistortionModelHandlerBase* LensDistortionHandler = nullptr;

	/** Cached distortion MID the handler produced. Used to clean up old one in case it changes */
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* LastDistortionMID = nullptr;

	/** Original focal length settings of the attached cinecamera component to reapply when distortion isn't applied anymore */
	UPROPERTY()
	float UndistortedFocalLength = 50.0f;

	/** Original cinecamera component rotation that we set back on when nodal offset isn't applied anymore */
	UPROPERTY()
	FRotator OriginalCameraRotation;

	/** Original cinecamera component location that we set back on when nodal offset isn't applied anymore */
	UPROPERTY()
	FVector OriginalCameraLocation;

	/** Unique identifier representing the source of distortion data */
	UPROPERTY(DuplicateTransient)
	FGuid DistortionProducerID;

	/** Used to control which data from LiveLink is actually applied to camera */
	UPROPERTY(EditAnywhere, Category="Settings")
	FLiveLinkCameraControllerUpdateFlags UpdateFlags;

#if WITH_EDITORONLY_DATA
	/** Whether to refresh frustum drawing on value change */
	UPROPERTY(EditAnywhere, Category="Debug")
	bool bShouldUpdateVisualComponentOnChange = true;
#endif

private:
	
	/** Whether incoming data requires encoder mapping */
	bool bIsEncoderMappingNeeded = false;

	/** Keep track of what needs to be setup to apply distortion */
	bool bIsDistortionSetup = false;

	/** Timestamp when we made the last warning log. Intervals to avoid log spamming */
	double LastInvalidLoggingLoggedTimestamp = 0.0f;
	static constexpr float TimeBetweenLoggingSeconds = 10.0f;

	//Last values used to detect changes made by the user and update our original caches
	float LastFocalLength = -1.0f;
	FCameraFilmbackSettings LastFilmback;
	FRotator LastRotation;
	FVector LastLocation;

public:

	ULiveLinkCameraController();

	//~ Begin ULiveLinkControllerBase interface
	virtual void Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData) override;
	virtual bool IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport) override;
	virtual TSubclassOf<UActorComponent> GetDesiredComponentClass() const override;
	virtual void SetAttachedComponent(UActorComponent* ActorComponent) override;
	virtual void Cleanup() override;
	virtual void OnEvaluateRegistered() override;
	//~ End ULiveLinkControllerBase interface

	//~ Begin UObject interface
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostEditImport() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif	
	//~ End UObject interface

	/** Returns true if encoder mapping is required for FIZ data */
	bool IsEncoderMappingNeeded() const { return bIsEncoderMappingNeeded; }

	/** Make sure what is required for distortion is setup or cleaned whether we apply or not distortion */
	void UpdateDistortionSetup();

protected:

	/** Applies FIZ data coming from LiveLink stream. Lens file is used if encoder mapping is required  */
	void ApplyFIZ(ULensFile* LensFile, UCineCameraComponent* CineCameraComponent, const FLiveLinkCameraStaticData* StaticData, const FLiveLinkCameraFrameData* FrameData);

	/** Applies nodal offset from lens file for the given Focus/Zoom values of CineCamera */
	void ApplyNodalOffset(ULensFile* LensFile, UCineCameraComponent* CineCameraComponent);

	/** Update distortion state */
	void ApplyDistortion(ULensFile* LensFile, UCineCameraComponent* CineCameraComponent);

	/** Refresh distortion handler's, in case user deletes it  */
	void UpdateDistortionHandler(UCineCameraComponent* CineCameraComponent);

	/** Update cached focal length in case it was changed */
	void UpdateCachedFocalLength(UCineCameraComponent* CineCameraComponent);

	/** Cleanup distortion objects we could have added to camera */
	void CleanupDistortion();

	/** Verify base transform and apply nodal offset on top of everything else done in tick */
	void OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
};
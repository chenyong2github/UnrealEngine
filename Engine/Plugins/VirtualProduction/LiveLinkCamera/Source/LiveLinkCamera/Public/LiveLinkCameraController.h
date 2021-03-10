// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkControllerBase.h"

#include "CineCameraComponent.h"
#include "Controllers/LiveLinkTransformController.h"
#include "Engine/EngineTypes.h"
#include "LensDistortionDataHandler.h"
#include "LensFile.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "LiveLinkCameraController.generated.h"


struct FLiveLinkCameraStaticData;
struct FLiveLinkCameraFrameData;

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

	/** Asset containing encoder and fiz mapping */
	UPROPERTY(EditAnywhere, Category = "Lens")
	FLensFilePicker LensFilePicker;

	/** Apply nodel offset from lens file if enabled */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens")
	bool bApplyNodalOffset = true;

protected:
	/** Whether or not to apply a post-process distortion effect directly to the attached CineCamera */
	UPROPERTY(EditAnywhere, Category = "Lens Distortion")
	bool bApplyDistortion = false;

	/** Cached distortion handler associated with attached camera component */
	UPROPERTY(EditAnywhere, Category = "Lens Distortion", Transient)
	ULensDistortionDataHandler* LensDistortionHandler = nullptr;

	/** Cached distortion MID the handler produced. Used to clean up old one in case it changes */
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* LastDistortionMID = nullptr;

	/** Original filmback settings of the attached cinecamera component to reapply when distortion isn't applied anymore */
	UPROPERTY()
	FCameraFilmbackSettings OriginalCameraFilmback;

	/** Original cinecamera component rotation that we set back on when nodal offset isn't applied anymore */
	UPROPERTY()
	FRotator OriginalCameraRotation;

	/** Original cinecamera component location that we set back on when nodal offset isn't applied anymore */
	UPROPERTY()
	FVector OriginalCameraLocation;

private:
	
	/** Whether incoming data requires encoder mapping */
	bool bIsEncoderMappingNeeded = false;

	/** Keep track of what needs to be setup to apply distortion */
	bool bIsDistortionSetup = false;

	/** Timestamp when we made the last warning log. Intervals to avoid log spamming */
	double LastInvalidLoggingLoggedTimestamp = 0.0f;
	static constexpr float TimeBetweenLoggingSeconds = 2.0f;

	//Last values used to detect changes made by the user and update our original caches
	FCameraFilmbackSettings LastCameraFilmback;
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

	/** Update cached filmback in case it was changed */
	void UpdateCachedFilmback(UCineCameraComponent* CineCameraComponent);

	/** Cleanup distortion objects we could have added to camera */
	void CleanupDistortion();

	/** Verify base transform and apply nodal offset on top of everything else done in tick */
	void OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
};
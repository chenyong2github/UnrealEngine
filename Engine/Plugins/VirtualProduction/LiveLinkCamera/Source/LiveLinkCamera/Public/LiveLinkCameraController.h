// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkControllerBase.h"

#include "CineCameraComponent.h"
#include "Controllers/LiveLinkTransformController.h"
#include "Engine/EngineTypes.h"
#include "LensDistortionAPI.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "LensFile.h"

#include "LiveLinkCameraController.generated.h"


USTRUCT(BlueprintType)
struct LIVELINKCAMERA_API FLensDistortionConfiguration
{
	GENERATED_BODY()
	
	// Whether lens distortion will be applied to the attached CineCameraComponent
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Live Link")
	bool bApplyDistortion = false;

	// Whether the overscan factor will be derived from the lens properties, or overriden with a user-entered value
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Live Link", meta = (EditCondition = bApplyDistortion))
	bool bOverrideOverscanFactor = false;

	// Factor by which the CineCamera's field of view will be scaled
	UPROPERTY(EditAnywhere, Category = "Live Link", meta = (ClampMin = "1.0", ClampMax = "1.5", EditCondition = bApplyDistortion))
	float OverscanFactor = 1.0f;

	// Amount by which to nudge the derived overscan factor up or down
	UPROPERTY(EditAnywhere, Category = "Live Link", meta = (ClampMin = "-0.5", ClampMax = "0.5", EditCondition = bApplyDistortion))
	float OverscanNudge = 0.0f;

	// Whether to enable a debug view that will show pixels with invalid distorted UV coordinates
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Live Link", meta = (EditCondition = bApplyDistortion))
	bool bEnableDistortionDebugView = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Live Link")
	UMaterialInstanceDynamic* LensDistortionMID = nullptr;
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

	/** Asset containing encoder and fiz mapping */
	UPROPERTY(EditAnywhere, Category = "Lens")
	FLensFilePicker LensFilePicker;

	/** Whether to apply lens distortion to the binded camera component */
	UPROPERTY(EditAnywhere, Category = "Lens")
	FLensDistortionConfiguration LensDistortionSettings;

private:
	
	/** Whether incoming data requires encoder mapping */
	bool bIsEncoderMappingNeeded = false;

	/** Keep track of what needs to be setup to apply distortion */
	bool bIsDistortionSetup = false;

	/** Original filmback settings of the attached cinecamera component */
	FCameraFilmbackSettings OriginalCameraFilmback;

	/** Timestamp when we made the last warning log. Intervals to avoid log spamming */
	double LastInvalidLoggingLoggedTimestamp = 0.0f;
	static constexpr float TimeBetweenLoggingSeconds = 2.0f;

public:
	//~ Begin ULiveLinkControllerBase interface
	virtual void Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData) override;
	virtual bool IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport) override;
	virtual TSubclassOf<UActorComponent> GetDesiredComponentClass() const override;
	virtual void OnEvaluateRegistered() override;
	//~ End ULiveLinkControllerBase interface

	//~ Begin UObject interface
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif	
	//~ End UObject interface

	/** Returns true if encoder mapping is required for FIZ data */
	bool IsEncoderMappingNeeded() const { return bIsEncoderMappingNeeded; }

	/** Make sure what is required for distortion is setup or cleaned whether we apply or not distortion */
	void UpdateDistortionSetup();
};